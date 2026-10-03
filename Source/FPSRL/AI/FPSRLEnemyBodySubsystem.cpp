// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyBodySubsystem.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "FPSRL.h"

namespace FPSRLEnemyBody
{
	static const FName BodyName(TEXT("RoleBody"));
}

bool UFPSRLEnemyBodySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UFPSRLEnemyBodySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
	for (TActorIterator<APawn> It(&InWorld); It; ++It)
	{
		ApplyBody(*It);	// enemies placed in the level
	}
}

void UFPSRLEnemyBodySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
	}
	Super::Deinitialize();
}

void UFPSRLEnemyBodySubsystem::HandleActorSpawned(AActor* Actor)
{
	ApplyBody(Cast<APawn>(Actor));
}

void UFPSRLEnemyBodySubsystem::ApplyBody(APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	USkeletalMeshComponent* Skeleton = Character ? Character->GetMesh() : nullptr;
	if (!Skeleton || Character->IsPlayerControlled() || Character->FindComponentByTag<USkeletalMeshComponent>(FPSRLEnemyBody::BodyName))
	{
		return;
	}
	const UFPSRLEnemyDefinition* Definition = UFPSRLEnemyScalingSettings::Get().FindDefinition(Character->GetClass());
	if (!Definition || Definition->BodyMesh.IsNull())
	{
		return;
	}
	USkeletalMesh* BodyMesh = Definition->BodyMesh.LoadSynchronous();
	if (!BodyMesh)
	{
		static bool bWarned = false;
		UE_CLOG(!bWarned, LogFPSRL, Warning, TEXT("[Enemy] body %s not found (art not on this machine?): the enemy keeps its own mesh"), *Definition->BodyMesh.ToString());
		bWarned = true;
		return;
	}

	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Character, FPSRLEnemyBody::BodyName);
	Body->ComponentTags.Add(FPSRLEnemyBody::BodyName);
	Body->SetupAttachment(Skeleton);
	Body->SetSkeletalMesh(BodyMesh);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->RegisterComponent();
	Character->AddInstanceComponent(Body);
	Body->SetLeaderPoseComponent(Skeleton);

	// The original mesh still animates (the body copies its pose) but isn't drawn; the weapon attached to it stays.
	Skeleton->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Skeleton->SetVisibility(false, false);

	if (UFPSRLHealthComponent* Health = Character->FindComponentByClass<UFPSRLHealthComponent>())
	{
		Health->SetKeepsOwnColours();	// the role's own look, no testing tint
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Enemy] %s wears %s"), *Character->GetName(), *BodyMesh->GetName());
}
