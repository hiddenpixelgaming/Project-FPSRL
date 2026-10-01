// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLDepthLayoutComponent.generated.h"

class APlayerController;
class APlayerState;
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

	/** Combat and Miniboss rooms: the altar it gives (rolled from the Depth's reward odds in room order, spawned
	 *  by its exit door when cleared). None for every other room. */
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
 *   Entry map -> [Preparation] -> Combat -> Traversal -> Combat -> ... -> Combat -> [Miniboss] -> [Traversal -> Final Level
 *   Boss arena] -> Exit room. Each room's origin is chained onto the previous room's exit. Every Combat and Miniboss room
 *   gives one altar (Blessing / Upgrade / Healing, rolled in room order from the Depth's reward odds), spawned beside its
 *   exit door when the room loads, locked (dark) until it is cleared. No other room ever gets one (user rule); the
 *   Preparation room has its own altars placed in its level.
 *
 * Streaming (server decides, every machine follows): only a window of the sequence is loaded. It reaches one room past
 * the first uncleared encounter, so clearing a combat room starts loading the next one while the party crosses the
 * traversal; it starts one room behind the rearmost player, so rooms everyone has left behind are unloaded. Every
 * machine streams the window with the same instance names, so placed actors (doors, rooms, portal) match up over the
 * network. Clients report each room they have loaded (AFPSRLPlayerController::ServerReportRoomShown); ReadyThrough is
 * the last room loaded on EVERY machine, and a traversal's exit gate (AFPSRLTransitionGate) opens only once the room
 * after it is ready everywhere.
 *
 * Completion: a Depth's required encounters are its Combat, Miniboss and Final Level Boss rooms, known from the sequence
 * before they load, and remembered here once cleared, so unloading a room never undoes its completion.
 *
 * Expedition progress inside a Depth (server-authoritative; players move at their own pace):
 *  - Each room's exit door unlocks when its encounter is cleared (AFPSRLRoom); anyone may then go on, nobody waits.
 *  - Player room tracking: each player's room (AFPSRLPlayerState::ExpeditionRoomIndex) comes from where their pawn is,
 *    on the occupancy timer. The first player into a new room is announced to the others.
 *  - ActiveEncounterIndex: the encounter room most recently started. A room starts when the first player walks through
 *    its combat trigger; it never waits for the others and never pauses for them.
 *  - Catch-up: CatchUpOfferDelay after a room starts, every player who is up and still in an earlier room gets their
 *    OWN optional offer (AFPSRLPlayerController::ClientCatchUpOffer). Accepting asks the server
 *    (ServerRequestCatchUp), which re-validates everything and moves them to a safe spot at that room's entrance, before
 *    its combat trigger, never next to a teammate or into the fight. Declining or ignoring changes nothing. One offer
 *    per player per started room; only ever to the active room, and only while its encounter is on.
 *  - The Depth's exit portal stays a party decision (AFPSRLExitPortal): it moves the whole session to the next map.
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

	/** Combat, Miniboss and Final Level Boss rooms count toward the Depth; everything else never does. */
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

	/** The encounter room most recently started (INDEX_NONE before the first). Catch-up only ever targets it. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depth")
	int32 ActiveEncounterIndex = INDEX_NONE;

	/** A room's name for players ("Ember Causeway", else its type). */
	FText GetRoomDisplayName(int32 Index) const;

	/** Server: a player accepted / declined their catch-up offer for room RoomIndex (from their controller's RPC). */
	void RequestCatchUp(APlayerController* Player, int32 RoomIndex);
	void DeclineCatchUp(APlayerController* Player, int32 RoomIndex);

	/** Server: refresh every player's ExpeditionRoomIndex now (also runs on the occupancy timer). */
	void UpdatePlayerRooms();

	/** Server, debugging: one line per player and the active room ([Progress]). */
	FString DescribeProgress() const;

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

	// --- Catch-up (server only) ---
	enum class ECatchUpResponse : uint8 { Pending, Accepted, Declined, Expired };
	struct FCatchUpOffer
	{
		int32 RoomIndex = INDEX_NONE;
		ECatchUpResponse Response = ECatchUpResponse::Pending;
	};
	/** The latest offer each player got (one per started room). */
	TMap<TWeakObjectPtr<APlayerState>, FCatchUpOffer> CatchUpOffers;
	FTimerHandle CatchUpOfferTimer;
	/** The furthest room any player has entered (for "X moved on" announcements). */
	int32 FurthestRoomReached = INDEX_NONE;

	/** Server: offer catch-up for RoomIndex to every eligible player still behind it. */
	void OfferCatchUp(int32 RoomIndex);

	/** Server: the room's encounter is over or the player got there: their pending offer goes away. */
	void ExpireCatchUpOffers(int32 RoomIndex, bool bOnlyArrived);

	/** Server: a safe arrival spot at RoomIndex's entrance for Pawn (false: none safe right now). */
	bool FindCatchUpSpot(int32 RoomIndex, const APawn* Pawn, FTransform& OutSpot, FString& OutReason) const;

	bool IsCatchUpAllowedFor(int32 RoomIndex) const;

	/** Which room a world position is in (the highest index whose bounds hold it, INDEX_NONE if none). */
	int32 FindRoomAt(const FVector& Location) const;
	bool bLayoutPending = false;
	bool bFirstWindowShown = false;
};
