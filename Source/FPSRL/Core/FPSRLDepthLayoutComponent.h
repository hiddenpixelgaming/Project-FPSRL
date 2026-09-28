// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLDepthLayoutComponent.generated.h"

class APlayerController;
class UFPSRLDepthDefinition;
class UFPSRLRoomDefinition;
class ULevelStreamingDynamic;

/** One room of the Depth's sequence: which room level, where its origin sits, and (traversals) what it holds. */
USTRUCT(BlueprintType)
struct FFPSRLRoomPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	TObjectPtr<UFPSRLRoomDefinition> Room;

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	FTransform Transform;

	/** Traversal spaces: rolled from the Depth's TraversalRewards. None for every other room. */
	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	EFPSRLTraversalReward Reward = EFPSRLTraversalReward::None;
};

/** The range of placements that should be loaded right now (inclusive). Empty when Last < First. */
USTRUCT(BlueprintType)
struct FFPSRLStreamWindow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	int32 First = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	int32 Last = -1;

	bool Contains(int32 Index) const { return Index >= First && Index <= Last; }
	bool operator==(const FFPSRLStreamWindow& Other) const { return First == Other.First && Last == Other.Last; }
};

DECLARE_MULTICAST_DELEGATE(FFPSRLLayoutEvent);

/**
 * Room sequencing and room-by-room streaming for one Depth (lives on AFPSRLGameState).
 *
 * Sequence (server, rolled once from the Depth definition's pools, then replicated):
 *   Entry map -> [Preparation] -> Combat -> Traversal -> Combat -> ... -> Combat -> [Elite] -> [Traversal -> Final Level
 *   Boss arena] -> Exit room. Each room's origin is chained onto the previous room's exit. Traversals carry their rolled
 *   reward (Blessing Altar, Upgrade Altar, ...), spawned at the traversal's AFPSRLRewardSpawnPoint when it loads.
 *
 * Streaming (server decides, every machine follows): only a window of the sequence is loaded. It reaches one room past
 * the first uncleared encounter, so clearing a combat room starts loading the next one while the party crosses the
 * traversal; it starts one room behind the rearmost player, so rooms everyone has left behind are unloaded. Every
 * machine streams the window with the same instance names, so placed actors (doors, rooms, portal) match up over the
 * network. Clients report each room they have loaded (AFPSRLPlayerController::ServerReportRoomShown); ReadyThrough is
 * the last room loaded on EVERY machine, and a traversal's exit gate (AFPSRLTransitionGate) opens only once the room
 * after it is ready everywhere.
 *
 * Completion: a Depth's required encounters are its Combat, Elite and Final Level Boss rooms, known from the sequence
 * before they load, and remembered here once cleared, so unloading a room never undoes its completion.
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLDepthLayoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLDepthLayoutComponent();

	UPROPERTY(ReplicatedUsing = OnRep_Placements, BlueprintReadOnly, Category = "Depth")
	TArray<FFPSRLRoomPlacement> Placements;

	/** Which placements should be loaded (server decides). */
	UPROPERTY(ReplicatedUsing = OnRep_Window, BlueprintReadOnly, Category = "Depth")
	FFPSRLStreamWindow Window;

	/** The last placement loaded on every player's machine (-1 = none yet). */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyThrough, BlueprintReadOnly, Category = "Depth")
	int32 ReadyThrough = -1;

	/** Per placement: streaming and encounter state (server-maintained, for UI and debugging). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	TArray<EFPSRLRoomState> RoomStates;

	/** Per placement: its required encounter is cleared. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	TArray<bool> EncounterCleared;

	/** Server: the first window of rooms is loaded and visible (the Depth may evaluate its encounters). */
	FFPSRLLayoutEvent OnLayoutReady;

	/** Server: a placement's RoomStates entry changed. */
	FFPSRLLayoutEvent OnRoomStateChanged;

	/** Every machine: ReadyThrough changed (transition gates listen). */
	FFPSRLLayoutEvent OnReadinessChanged;

	/** Server: roll the sequence and start streaming. False if the Depth has no room pools (single handcrafted map). */
	bool BuildLayout(const UFPSRLDepthDefinition* Depth);

	bool HasLayout() const { return !Placements.IsEmpty(); }
	bool IsLayoutPending() const { return bLayoutPending; }

	/** Combat, Elite and Final Level Boss rooms count toward the Depth; everything else never does. */
	static bool IsEncounterRoom(ERoomType Type);

	int32 GetRequiredEncounterCount() const;
	int32 GetClearedEncounterCount() const;

	/** Which placement an actor placed in a streamed room belongs to (INDEX_NONE if the entry map or unknown). */
	int32 FindPlacementIndex(const AActor* Actor) const;

	/** Server: a room's encounter started / was cleared. */
	void NotifyEncounterStarted(const AActor* RoomActor);
	void NotifyEncounterCleared(const AActor* RoomActor);

	/** Server: a client has loaded and shown this placement. */
	void ReportRoomShown(APlayerController* Player, int32 Index);

	/** Server: where to put back a player who fell from around FellFrom (the start of the nearest loaded room). */
	bool FindSafeSpot(const FVector& FellFrom, FTransform& OutSpot) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_Placements();

	UFUNCTION()
	void OnRep_Window();

	UFUNCTION()
	void OnRep_ReadyThrough();

	/** Any streamed room became visible or was hidden (the engine doesn't say which, so every room is checked). */
	UFUNCTION()
	void HandleStreamingChanged();

	/** Client: send the server any room loaded here that it hasn't counted yet; retries every second until it has. */
	void ResendUnconfirmedRooms();

	TArray<FFPSRLRoomPlacement> RollSequence(const UFPSRLDepthDefinition& Depth) const;
	FTransform FindEntryTransform() const;

	/** Server: recompute the window from the encounters and where the players are, and apply it. */
	void UpdateWindow();

	/** Every machine: load what the window holds and unload what it doesn't. */
	void ApplyWindow();

	/** Server: ReadyThrough = the last room loaded on every connected player's machine. */
	void RecomputeReadiness();

	/** Server: unload rooms every player has moved past (runs on a timer, not Tick). */
	void CheckOccupancy();

	/** Server: a placement just became visible here (bounds, traversal reward, state). */
	void OnPlacementShownOnServer(int32 Index);

	/** Server: a placement is about to unload (clear what the server spawned into it). */
	void OnPlacementUnloading(int32 Index);

	void SetRoomState(int32 Index, EFPSRLRoomState State);

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ULevelStreamingDynamic>> StreamedRooms;

	/** This machine: placements currently visible. */
	TSet<int32> ShownHere;

	// Server only.
	TMap<TWeakObjectPtr<APlayerController>, TSet<int32>> ShownByClient;
	TMap<int32, FBox> PlacementBounds;
	TMap<int32, TWeakObjectPtr<AActor>> RewardActors;
	TWeakObjectPtr<AActor> FallVolume;
	FTimerHandle OccupancyTimer;
	/** Client: retries room reports the server hasn't counted yet (see ResendUnconfirmedRooms). */
	FTimerHandle ReportRetryTimer;
	/** Rooms before this index may be unloaded (everyone has moved past them). Only grows. */
	int32 KeepFrom = 0;
	bool bLayoutPending = false;
	bool bFirstWindowShown = false;
};
