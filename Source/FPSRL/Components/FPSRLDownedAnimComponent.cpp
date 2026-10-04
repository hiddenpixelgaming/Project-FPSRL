// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLDownedAnimComponent.h"
#include "AI/FPSRLEnemyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "FPSRL.h"

void UFPSRLDownedAnimComponent::SetDowned(bool bDowned, bool bDied)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	if (bDowned)
	{
		UAnimSequence* Idle = Settings.DownedIdleAnim.LoadSynchronous();
		UAnimSequence* Crawl = Settings.DownedCrawlAnim.LoadSynchronous();
		if (!Idle || !Crawl)
		{
			return;	// the art isn't on this machine
		}
		GetWorld()->GetTimerManager().ClearTimer(RestoreTimer);
		if (!bShowingDowned)
		{
			SavedAnimClass = Mesh->GetAnimClass();
			Mesh->SetAnimInstanceClass(UFPSRLEnemyAnimInstance::StaticClass());
		}
		bShowingDowned = true;
		if (UFPSRLEnemyAnimInstance* Anim = Cast<UFPSRLEnemyAnimInstance>(Mesh->GetAnimInstance()))
		{
			Anim->SetLocomotion(Idle, Crawl, Settings.DownedCrawlAnimSpeed);
			Anim->PlayAction(Settings.DownedFallAnim.LoadSynchronous(), 0.f, 0.f, 1.f);	// falls, then writhes / crawls
		}
		UE_LOG(LogFPSRL, Verbose, TEXT("[Downed] %s falls (%s)"), *Character->GetName(), Character->HasAuthority() ? TEXT("server") : TEXT("client"));
		return;
	}
	if (!bShowingDowned)
	{
		return;
	}
	UAnimSequence* StandUp = Settings.DownedStandUpAnim.LoadSynchronous();
	UFPSRLEnemyAnimInstance* Anim = Cast<UFPSRLEnemyAnimInstance>(Mesh->GetAnimInstance());
	if (bDied || !StandUp || !Anim)
	{
		Restore();
		return;
	}
	// Revived: stands up, then its own animation again.
	Anim->PlayAction(StandUp, 0.f, 0.f, 1.f);
	GetWorld()->GetTimerManager().SetTimer(RestoreTimer, this, &ThisClass::Restore, FMath::Max(0.1f, StandUp->GetPlayLength() - 0.1f), false);
	UE_LOG(LogFPSRL, Verbose, TEXT("[Downed] %s stands up (%s)"), *Character->GetName(), Character->HasAuthority() ? TEXT("server") : TEXT("client"));
}

void UFPSRLDownedAnimComponent::Restore()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr; Mesh && bShowingDowned)
	{
		Mesh->SetAnimInstanceClass(SavedAnimClass);
	}
	bShowingDowned = false;
}
