// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyRoleComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/FPSRLWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "FPSRL.h"

namespace FPSRLEnemyRole
{
	static const FName Slot(TEXT("DefaultSlot"));
	static const TCHAR* LineMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* LineMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyAimLine.M_EnemyAimLine");
	static constexpr float Thin = 0.025f;		// cylinder scale: 2.5 cm
	static constexpr float Thick = 0.07f;		// the last moments before the shot
	static constexpr float LockSeconds = 0.3f;
}

UFPSRLEnemyRoleComponent::UFPSRLEnemyRoleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UFPSRLEnemyRoleComponent::StartAttack(FName AttackName, float WindupSeconds, AActor* Target, bool bAimLine)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MulticastStartAttack(AttackName, WindupSeconds, Target, bAimLine);
	}
}

void UFPSRLEnemyRoleComponent::CancelAttack()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MulticastCancelAttack();
	}
}

void UFPSRLEnemyRoleComponent::MulticastStartAttack_Implementation(FName AttackName, float WindupSeconds, AActor* Target, bool bAimLine)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	const UFPSRLEnemyDefinition* Definition = UFPSRLEnemyScalingSettings::Get().FindDefinition(Character->GetClass());
	const FFPSRLEnemyAttackAnim* AttackAnim = Definition ? Definition->AttackAnims.Find(AttackName) : nullptr;
	UAnimSequenceBase* Animation = AttackAnim ? AttackAnim->Animation.LoadSynchronous() : nullptr;
	UAnimInstance* AnimInstance = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (Animation && AnimInstance)
	{
		// Played so its impact frame lands when the attack executes (after the wind-up).
		const float PlayRate = WindupSeconds > 0.05f ? FMath::Clamp(AttackAnim->ImpactTime / WindupSeconds, 0.4f, 2.5f) : 1.f;
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Animation, FPSRLEnemyRole::Slot, 0.12f, 0.25f, PlayRate);
		LastAnimLength = Animation->GetPlayLength() / PlayRate;
	}
	if (bAimLine && Target && GetNetMode() != NM_DedicatedServer)
	{
		ShowAimLine(Target, WindupSeconds);
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Enemy] %s %s: animation %s, aim line %s (%s)"), *Character->GetName(), *AttackName.ToString(),
		Animation ? *Animation->GetName() : TEXT("none"), bAimLine ? TEXT("on") : TEXT("off"), Character->HasAuthority() ? TEXT("server") : TEXT("client"));
}

void UFPSRLEnemyRoleComponent::MulticastCancelAttack_Implementation()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (UAnimInstance* AnimInstance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->StopSlotAnimation(0.15f, FPSRLEnemyRole::Slot);
	}
	HideAimLine();
}

void UFPSRLEnemyRoleComponent::ShowAimLine(AActor* Target, float Seconds)
{
	if (!AimLine)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, FPSRLEnemyRole::LineMesh);
		if (!Mesh)
		{
			return;
		}
		AimLine = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("AimLine"));
		AimLine->SetStaticMesh(Mesh);
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, FPSRLEnemyRole::LineMaterial))
		{
			AimLine->SetMaterial(0, Material);
		}
		AimLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AimLine->SetCastShadow(false);
		AimLine->SetUsingAbsoluteLocation(true);
		AimLine->SetUsingAbsoluteRotation(true);
		AimLine->SetUsingAbsoluteScale(true);
		AimLine->SetupAttachment(GetOwner()->GetRootComponent());
		AimLine->RegisterComponent();
	}
	AimTarget = Target;
	AimLineStartTime = GetWorld()->GetTimeSeconds();
	AimLineEndTime = AimLineStartTime + Seconds;
	AimLine->SetVisibility(true);
	SetComponentTickEnabled(true);
	TickComponent(0.f, LEVELTICK_All, nullptr);
	GetWorld()->GetTimerManager().SetTimer(AimLineTimer, this, &ThisClass::HideAimLine, FMath::Max(0.05f, Seconds), false);
}

void UFPSRLEnemyRoleComponent::HideAimLine()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AimLineTimer);
	}
	if (AimLine)
	{
		AimLine->SetVisibility(false);
	}
	AimTarget.Reset();
	SetComponentTickEnabled(false);
}

FVector UFPSRLEnemyRoleComponent::GetAimLineStart() const
{
	// Its gun's muzzle (the visible weapon mesh), else its chest.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, true, true);
	for (const AActor* Actor : Attached)
	{
		if (!Actor->IsA<AFPSRLWeapon>())
		{
			continue;
		}
		TArray<USkeletalMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (const USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh->IsVisible() && Mesh->DoesSocketExist(TEXT("Muzzle")))
			{
				return Mesh->GetSocketLocation(TEXT("Muzzle"));
			}
		}
	}
	return Pawn->GetActorLocation() + FVector(0.f, 0.f, 40.f);
}

void UFPSRLEnemyRoleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (ThisTickFunction)
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
	const AActor* Target = AimTarget.Get();
	if (!AimLine || !Target)
	{
		HideAimLine();
		return;
	}
	const FVector Start = GetAimLineStart();
	const FVector End = Target->GetActorLocation();
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	// Thicker for the last moments: the shot is coming.
	const bool bLocking = AimLineEndTime - GetWorld()->GetTimeSeconds() < FPSRLEnemyRole::LockSeconds;
	const float Width = bLocking ? FPSRLEnemyRole::Thick : FPSRLEnemyRole::Thin;
	AimLine->SetWorldLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).Rotator());
	AimLine->SetWorldScale3D(FVector(Width, Width, Length / 100.f));	// the engine cylinder is 100 cm tall
}

void UFPSRLEnemyRoleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideAimLine();
	Super::EndPlay(EndPlayReason);
}

bool UFPSRLEnemyRoleComponent::IsAimLineShowing() const
{
	return AimLine && AimLine->IsVisible();
}
