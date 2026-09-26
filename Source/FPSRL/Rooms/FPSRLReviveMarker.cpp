// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLReviveMarker.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "FPSRL.h"

AFPSRLReviveMarker::AFPSRLReviveMarker()
{
	bReplicates = true;
	SetReplicatingMovement(true);	// replicates the attachment to the downed pawn
	PromptText = TEXT("Press E to Revive");

	ReviveRange = CreateDefaultSubobject<USphereComponent>(TEXT("ReviveRange"));
	ReviveRange->InitSphereRadius(180.f);
	ReviveRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ReviveRange->SetCollisionObjectType(ECC_WorldDynamic);
	ReviveRange->SetCollisionResponseToAllChannels(ECR_Ignore);
	ReviveRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ReviveRange->SetGenerateOverlapEvents(true);
	SetRootComponent(ReviveRange);
}

void AFPSRLReviveMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSRLReviveMarker, Target);
	DOREPLIFETIME(AFPSRLReviveMarker, Reviver);
	DOREPLIFETIME(AFPSRLReviveMarker, ReviveEndTime);
}

void AFPSRLReviveMarker::SetTarget(APawn* InTarget)
{
	Target = InTarget;
	if (InTarget)
	{
		AttachToActor(InTarget, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		InTarget->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleTargetDestroyed);
	}
	ForceNetUpdate();
}

void AFPSRLReviveMarker::HandleTargetDestroyed(AActor* DestroyedActor)
{
	Destroy();
}

void AFPSRLReviveMarker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReviveTimer);

	// Revive finished (the marker goes with it), or the downed player died: take the bar down.
	if (bLocalProgressShown)
	{
		if (AFPSRLPlayerController* LocalPC = GetWorld() ? Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr)
		{
			LocalPC->HideReviveProgress();
		}
		bLocalProgressShown = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool AFPSRLReviveMarker::CanInteract() const
{
	// Local view: only a teammate who is up sees the prompt, and only while nobody else is already reviving.
	const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
	return Target && LocalPawn && LocalPawn != Target && UFPSRLHealthComponent::IsPawnUp(LocalPawn)
		&& (!Reviver || Reviver == LocalPC->PlayerState);
}

void AFPSRLReviveMarker::Interact_Implementation(APlayerController* User)
{
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(User); PC && CanInteract())
	{
		PC->ServerStartRevive(this);
	}
}

bool AFPSRLReviveMarker::TryStartRevive(APlayerController* ReviverPC)
{
	const APawn* ReviverPawn = ReviverPC ? ReviverPC->GetPawn() : nullptr;
	const UFPSRLHealthComponent* TargetHealth = Target ? Target->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	if (!HasAuthority() || !ReviverPawn || ReviverPawn == Target || !TargetHealth || !TargetHealth->IsDowned()
		|| !UFPSRLHealthComponent::IsPawnUp(ReviverPawn))
	{
		return false;
	}
	if (Reviver && Reviver != ReviverPC->PlayerState)
	{
		return false;	// someone else is already on it
	}
	if (FVector::Dist(ReviverPawn->GetActorLocation(), GetActorLocation()) > ReviveRange->GetScaledSphereRadius() + 120.f)
	{
		return false;
	}
	if (Reviver)
	{
		return true;	// already reviving; a repeated E changes nothing
	}

	Reviver = ReviverPC->PlayerState;
	ReviveEndTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + UFPSRLRunSettings::Get().ReviveSeconds;
	GetWorldTimerManager().SetTimer(ReviveTimer, this, &ThisClass::CheckRevive, 0.2f, true);
	UE_LOG(LogFPSRL, Log, TEXT("[Revive] %s started reviving %s"), *Reviver->GetPlayerName(), *GetNameSafe(Target));
	ForceNetUpdate();
	OnRep_Revive();
	return true;
}

void AFPSRLReviveMarker::CheckRevive()
{
	const APawn* ReviverPawn = Reviver ? Reviver->GetPawn() : nullptr;
	UFPSRLHealthComponent* TargetHealth = Target ? Target->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	const bool bStillValid = ReviverPawn && UFPSRLHealthComponent::IsPawnUp(ReviverPawn) && TargetHealth && TargetHealth->IsDowned()
		&& FVector::Dist(ReviverPawn->GetActorLocation(), GetActorLocation()) <= ReviveRange->GetScaledSphereRadius() + 120.f;
	if (!bStillValid)
	{
		CancelRevive();
		return;
	}
	if (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() >= ReviveEndTime)
	{
		GetWorldTimerManager().ClearTimer(ReviveTimer);
		UE_LOG(LogFPSRL, Log, TEXT("[Revive] %s revived %s"), *Reviver->GetPlayerName(), *GetNameSafe(Target));
		TargetHealth->Revive(UFPSRLRunSettings::Get().ReviveHealthFraction);	// destroys this marker
	}
}

void AFPSRLReviveMarker::CancelRevive()
{
	GetWorldTimerManager().ClearTimer(ReviveTimer);
	UE_LOG(LogFPSRL, Log, TEXT("[Revive] revive of %s cancelled"), *GetNameSafe(Target));
	Reviver = nullptr;
	ReviveEndTime = 0.0;
	ForceNetUpdate();
	OnRep_Revive();
}

void AFPSRLReviveMarker::OnRep_Revive()
{
	RefreshLocalInteractor();	// the prompt hides for others while someone revives

	// Progress bar for the reviver and the downed player (replicated times, so both see the same fill).
	AFPSRLPlayerController* LocalPC = GetWorld() ? Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
	if (!LocalPC || !LocalPC->IsLocalController())
	{
		return;
	}
	const bool bLocalIsReviver = Reviver && Reviver == LocalPC->PlayerState;
	const bool bLocalIsTarget = Target && Target == LocalPC->GetPawn();
	if (Reviver && ReviveEndTime > 0.0 && (bLocalIsReviver || bLocalIsTarget))
	{
		const APlayerState* TargetState = Target ? Target->GetPlayerState() : nullptr;
		const FText Label = bLocalIsReviver
			? FText::Format(NSLOCTEXT("FPSRL", "Reviving", "Reviving {0}... stay close"), FText::FromString(TargetState ? TargetState->GetPlayerName() : FString()))
			: FText::Format(NSLOCTEXT("FPSRL", "BeingRevived", "{0} is reviving you"), FText::FromString(Reviver->GetPlayerName()));
		LocalPC->ShowReviveProgress(Label, ReviveEndTime - UFPSRLRunSettings::Get().ReviveSeconds, ReviveEndTime);
		bLocalProgressShown = true;
	}
	else if (bLocalProgressShown)
	{
		LocalPC->HideReviveProgress(NSLOCTEXT("FPSRL", "ReviveInterrupted", "Revive interrupted"));
		bLocalProgressShown = false;
	}
}
