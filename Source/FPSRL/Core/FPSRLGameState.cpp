// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLGameState.h"
#include "Components/FPSRLBoonComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLBoonSettings.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "FPSRL.h"

void AFPSRLGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLGameState, bBoonSelectionActive);
	DOREPLIFETIME(AFPSRLGameState, BoonSelectionEventId);
	DOREPLIFETIME(AFPSRLGameState, BoonSelectionDeadline);
}

UFPSRLBoonComponent* AFPSRLGameState::GetBoonComponent(const APlayerState* PlayerState)
{
	const AFPSRLPlayerState* PS = Cast<AFPSRLPlayerState>(PlayerState);
	return PS ? PS->GetBoonComponent() : nullptr;
}

void AFPSRLGameState::StartBoonSelection()
{
	if (!HasAuthority() || bBoonSelectionActive)
	{
		return;
	}

	const float Timeout = UFPSRLBoonSettings::Get().SelectionTimeoutSeconds;
	++BoonSelectionEventId;
	BoonSelectionDeadline = GetServerWorldTimeSeconds() + Timeout;
	bBoonSelectionActive = true;
	ForceNetUpdate();

	UE_LOG(LogFPSRL, Log, TEXT("[Boons] Selection %d started for %d player(s), %.0fs"), BoonSelectionEventId, PlayerArray.Num(), Timeout);
	OnBoonSelectionStarted.Broadcast();

	// Each player gets their own independent set. Players with nothing to offer are resolved immediately.
	for (APlayerState* Player : PlayerArray)
	{
		if (UFPSRLBoonComponent* Boons = GetBoonComponent(Player))
		{
			Boons->BeginSelection(BoonSelectionEventId, BoonSelectionDeadline);
		}
	}

	GetWorldTimerManager().SetTimer(SelectionTimeoutHandle, this, &ThisClass::HandleSelectionTimeout, Timeout, false);
	CheckSelectionComplete();
}

void AFPSRLGameState::HandleSelectionTimeout()
{
	if (!bBoonSelectionActive)
	{
		return;
	}

	UE_LOG(LogFPSRL, Log, TEXT("[Boons] Selection %d timed out; auto-picking for pending players"), BoonSelectionEventId);
	const int32 EventId = BoonSelectionEventId;
	for (APlayerState* Player : PlayerArray)
	{
		if (UFPSRLBoonComponent* Boons = GetBoonComponent(Player))
		{
			Boons->AutoSelect(EventId);	// no-op for players who already chose
		}
	}
	CheckSelectionComplete();
}

void AFPSRLGameState::NotifyBoonSelectionResolved(int32 EventId)
{
	if (IsBoonSelectionEventActive(EventId))
	{
		CheckSelectionComplete();
	}
}

void AFPSRLGameState::RemovePlayerState(APlayerState* PlayerState)
{
	// A leaving player must never block the group: resolve them (without a grant) before re-checking.
	if (HasAuthority() && bBoonSelectionActive)
	{
		if (UFPSRLBoonComponent* Boons = GetBoonComponent(PlayerState))
		{
			Boons->ForceResolve(BoonSelectionEventId);
		}
	}

	Super::RemovePlayerState(PlayerState);

	if (HasAuthority() && bBoonSelectionActive)
	{
		CheckSelectionComplete();
	}
}

void AFPSRLGameState::CheckSelectionComplete()
{
	if (!HasAuthority() || !bBoonSelectionActive)
	{
		return;
	}

	for (const APlayerState* Player : PlayerArray)
	{
		const UFPSRLBoonComponent* Boons = GetBoonComponent(Player);
		if (Boons && !Player->IsInactive() && Boons->IsSelectionPending(BoonSelectionEventId))
		{
			return;
		}
	}

	// Everyone resolved: close exactly once.
	bBoonSelectionActive = false;
	GetWorldTimerManager().ClearTimer(SelectionTimeoutHandle);
	ForceNetUpdate();
	UE_LOG(LogFPSRL, Log, TEXT("[Boons] Selection %d complete"), BoonSelectionEventId);
	OnBoonSelectionComplete.Broadcast();
}

void AFPSRLGameState::OnRep_BoonSelectionActive()
{
	(bBoonSelectionActive ? OnBoonSelectionStarted : OnBoonSelectionComplete).Broadcast();
}
