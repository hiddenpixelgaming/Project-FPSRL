// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLGameState.generated.h"

class AFPSRLRoom;
class UFPSRLDepthLayoutComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLDepthEvent);

/**
 * GameState for Depth levels (BP_GameStateMatch derives from this). Owns the current Depth's completion state.
 *
 * Room / Depth / Area / Run completion are separate states:
 *  - Room: an AFPSRLRoom's required enemies are dead (the room reports it here).
 *  - Depth: every REQUIRED encounter of this Depth is complete -> OnDepthCompleted fires exactly once -> exit portals open.
 *    A generated Depth counts its Combat / Elite / Final Level Boss rooms from the room sequence (rooms stream in one
 *    by one and unload behind the party, so counting loaded rooms would undercount).
 *  - Final Level Boss: FinalLevelBossState follows the boss room; defeating it sets bLevelComplete (the level exit
 *    portal then leads wherever the run data says: the next Area, or back to the Lobby after the last one).
 *    A Depth with no required rooms (merchant / preparation) completes as soon as play begins, or, for a generated
 *    Depth, as soon as all of its rooms have streamed in.
 *  - Area / Run: advanced by UFPSRLRunSubsystem when the party takes the portal.
 *
 * Server-authoritative; clients see the replicated counters and get OnDepthCompleted through OnRep.
 * Boon choices are no longer a group phase here: they are personal and optional, at Boon altars (AFPSRLBoonTerminal),
 * and never hold up the Depth.
 */
UCLASS()
class FPSRL_API AFPSRLGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AFPSRLGameState();

	/** Rolls and streams this Depth's rooms when its definition has room pools (Phase 2 sequencing). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Depth")
	TObjectPtr<UFPSRLDepthLayoutComponent> DepthLayout;

	/** 1-based, for display. 0 when no run is active (PIE started directly in this map). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	int32 AreaNumber = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	int32 DepthNumber = 0;

	UPROPERTY(ReplicatedUsing = OnRep_DepthProgress, BlueprintReadOnly, Category = "Depth")
	int32 RequiredRooms = 0;

	UPROPERTY(ReplicatedUsing = OnRep_DepthProgress, BlueprintReadOnly, Category = "Depth")
	int32 CompletedRooms = 0;

	UPROPERTY(ReplicatedUsing = OnRep_DepthComplete, BlueprintReadOnly, Category = "Depth")
	bool bDepthComplete = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	EFPSRLDepthState DepthState = EFPSRLDepthState::NotStarted;

	/** This Depth's Final Level Boss (NotStarted when it has none or it isn't reached yet). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	EFPSRLBossState FinalLevelBossState = EFPSRLBossState::NotStarted;

	/** The Final Level Boss is defeated: the level is complete and its exit portal opens. */
	UPROPERTY(ReplicatedUsing = OnRep_LevelComplete, BlueprintReadOnly, Category = "Depth")
	bool bLevelComplete = false;

	/** Required-room counters changed (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "Depth")
	FFPSRLDepthEvent OnDepthProgressChanged;

	/** Every required encounter is done. Fires once per Depth, on server and clients. */
	UPROPERTY(BlueprintAssignable, Category = "Depth")
	FFPSRLDepthEvent OnDepthCompleted;

	UFUNCTION(BlueprintPure, Category = "Depth")
	int32 GetRemainingRooms() const { return FMath::Max(0, RequiredRooms - CompletedRooms); }

	/** Server: a required room joins this Depth (called by the room on BeginPlay). */
	void RegisterRequiredRoom(AFPSRLRoom* Room);

	/** Server: a room's encounter started. */
	void NotifyEncounterStarted(AFPSRLRoom* Room);

	/** Server: a room finished its encounter. */
	void NotifyRoomCompleted(AFPSRLRoom* Room);

	/** Server: a player left the game (after their PlayerState is removed). */
	FSimpleMulticastDelegate OnPlayerLeft;

	virtual void RemovePlayerState(APlayerState* PlayerState) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_DepthProgress();

	UFUNCTION()
	void OnRep_DepthComplete();

	/** Everyone sees "Level Complete" when the Final Level Boss falls (the listen host calls it directly). */
	UFUNCTION()
	void OnRep_LevelComplete();

private:
	void RecountRooms();
	void EvaluateEmptyDepth();
	void CompleteDepth();
	void RefreshFinalLevelBossState();

	FTimerHandle CompletedTimer;

	TArray<TWeakObjectPtr<AFPSRLRoom>> RequiredRoomList;
};
