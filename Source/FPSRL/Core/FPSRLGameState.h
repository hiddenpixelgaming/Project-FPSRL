// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FPSRLGameState.generated.h"

class AFPSRLRoom;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLDepthEvent);

/**
 * GameState for Depth levels (BP_GameStateMatch derives from this). Owns the current Depth's completion state.
 *
 * Room / Depth / Area / Run completion are separate states:
 *  - Room: an AFPSRLRoom's required enemies are dead (the room reports it here).
 *  - Depth: every REQUIRED room of this Depth is complete -> OnDepthCompleted fires exactly once -> exit portals open.
 *    A Depth with no required rooms (merchant / preparation) completes as soon as play begins.
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

private:
	void RecountRooms();
	void CompleteDepth();

	TArray<TWeakObjectPtr<AFPSRLRoom>> RequiredRoomList;
};
