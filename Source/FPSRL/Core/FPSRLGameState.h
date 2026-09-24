// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FPSRLGameState.generated.h"

class UFPSRLBoonComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLBoonSelectionEvent);

/**
 * Run-level replicated state. BP_GameStateMatch derives from this.
 *
 * Boon selection phase (server-authoritative):
 *  1. StartBoonSelection() - called when an arena is cleared (BP_ArenaManager). Opens a personal selection for every
 *     player (3 options each, independent) and starts the timeout (UFPSRLBoonSettings::SelectionTimeoutSeconds).
 *  2. Each player resolves by picking (or has nothing to offer, e.g. full slots).
 *  3. On timeout the server auto-picks one of each pending player's CURRENT options.
 *  4. A player who leaves is treated as resolved, so they can never block the group.
 *  5. When every player is resolved, OnBoonSelectionComplete fires exactly once (the arena unlocks its exit then).
 * Clients see bBoonSelectionActive / BoonSelectionDeadline for UI; OnBoonSelectionComplete also fires on clients via OnRep.
 */
UCLASS()
class FPSRL_API AFPSRLGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(ReplicatedUsing = OnRep_BoonSelectionActive, BlueprintReadOnly, Category = "Boons")
	bool bBoonSelectionActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Boons")
	int32 BoonSelectionEventId = 0;

	/** Server world time at which pending players are auto-picked. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Boons")
	double BoonSelectionDeadline = 0.0;

	UPROPERTY(BlueprintAssignable, Category = "Boons")
	FFPSRLBoonSelectionEvent OnBoonSelectionStarted;

	/** Every player has resolved their selection. Fires once per selection event. */
	UPROPERTY(BlueprintAssignable, Category = "Boons")
	FFPSRLBoonSelectionEvent OnBoonSelectionComplete;

	/** Server-only. Ignored if a selection is already running. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boons")
	void StartBoonSelection();

	bool IsBoonSelectionEventActive(int32 EventId) const { return bBoonSelectionActive && EventId == BoonSelectionEventId; }

	/** Called by a player's boon component when it resolves. */
	void NotifyBoonSelectionResolved(int32 EventId);

	virtual void RemovePlayerState(APlayerState* PlayerState) override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_BoonSelectionActive();

private:
	void HandleSelectionTimeout();
	void CheckSelectionComplete();
	static UFPSRLBoonComponent* GetBoonComponent(const APlayerState* PlayerState);

	FTimerHandle SelectionTimeoutHandle;
};
