// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRunDefinition.generated.h"

class UFPSRLRoomDefinition;

/** Relative odds of what a Traversal space holds (weights; 0 or missing = never). */
USTRUCT(BlueprintType)
struct FFPSRLTraversalRewardOdds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	TMap<EFPSRLTraversalReward, float> Weights;

	/** Rolls one reward (None when every weight is 0). */
	EFPSRLTraversalReward Roll() const;
};

/**
 * One Depth of a run: a small set of handcrafted rooms fought through before the exit portal opens.
 *
 * Map is the Depth's entry room (player spawn + an AFPSRLRoomConnector at its exit). If the room pools below are
 * filled, the server rolls a room sequence from them each time the Depth is entered and streams those rooms in behind
 * the entry, room by room (UFPSRLDepthLayoutComponent):
 *   Entry -> [Preparation] -> Combat -> Traversal -> Combat -> ... -> Combat -> [Elite] -> [Traversal -> Final Level
 *   Boss arena] -> Exit room.
 * Combat rooms: exactly MinCombatRooms when Min == Max (4 in Area 1). Traversal spaces sit between combat rooms and
 * before the boss arena; each one's reward (none, Blessing Altar, Upgrade Altar, ...) is rolled from TraversalRewards.
 * Altars never count toward completion; only Combat, Elite and Final Level Boss encounters do.
 * With empty pools the Map alone is the Depth (a fully handcrafted Depth).
 */
UCLASS(BlueprintType)
class FPSRL_API UFPSRLDepthDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Depth")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Depth")
	EDepthType DepthType = EDepthType::Normal;

	/** Level loaded for this Depth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Depth")
	TSoftObjectPtr<UWorld> Map;

	/** False for a Depth that ends the run in place (the portal is Disabled). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Depth")
	bool bHasExitPortal = true;

	// --- Generation (Phase 2: room sequencing) ----------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
	int32 MinCombatRooms = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
	int32 MaxCombatRooms = 5;

	/** Combat rooms that must be cleared before the portal opens. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
	int32 GuaranteedCombatRooms = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
	int32 MinOptionalRooms = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0"))
	int32 MaxOptionalRooms = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation")
	bool bHasElite = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation")
	bool bHasBoss = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation")
	bool bHasMerchant = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0", ClampMax = "1"))
	float BoonAltarChance = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation", meta = (ClampMin = "0", ClampMax = "1"))
	float UpgradeStationChance = 0.f;

	/** A Traversal space between each pair of combat rooms (streaming, movement, optional altar). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation|Traversal")
	bool bTraversalBetweenCombatRooms = true;

	/** A Traversal space before the Final Level Boss arena (the boss transition). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation|Traversal")
	bool bTraversalBeforeBoss = true;

	/** Reward odds per traversal, in order (entry 0 = the first traversal). Traversals past the end use DefaultTraversalReward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation|Traversal")
	TArray<FFPSRLTraversalRewardOdds> TraversalRewards;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation|Traversal")
	FFPSRLTraversalRewardOdds DefaultTraversalReward;

	/** Reward odds for the traversal before the boss arena. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation|Traversal")
	FFPSRLTraversalRewardOdds BossTraversalReward;

	/** Starts with a Preparation room (safe room: altars, future Merchant), e.g. the Final Level Boss Depth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Generation")
	bool bHasPreparation = false;

	// --- Room pools (handcrafted room library) ----------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> CombatRooms;

	/** Used when bHasElite. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> EliteRooms;

	/** Reward / Boon / Merchant / Upgrade rooms; picked by their RoomType and the chances above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> OptionalRooms;

	/** Traversal spaces (corridors). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> TraversalRooms;

	/** Used when bHasPreparation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> PreparationRooms;

	/** Used when bHasBoss: the Final Level Boss arena, the last room before the exit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> BossRooms;

	/** The final room; normally holds the exit portal. Picked at random if several. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rooms")
	TArray<TObjectPtr<UFPSRLRoomDefinition>> ExitRooms;

	/** True if the Depth is assembled from pools (otherwise Map alone is the Depth). */
	bool UsesRoomPools() const { return !CombatRooms.IsEmpty() || !ExitRooms.IsEmpty() || !BossRooms.IsEmpty() || !PreparationRooms.IsEmpty(); }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Depth"), GetFName()); }
};

/** One Area (level): an ordered list of Depths, normally ending in the Final Level Boss. */
USTRUCT(BlueprintType)
struct FFPSRLAreaEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Area")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Area")
	TArray<TObjectPtr<UFPSRLDepthDefinition>> Depths;
};

/**
 * The whole expedition: Areas in order, each an ordered list of Depths. Progress is strictly forward.
 * Set the active one in Project Settings > FPSRL Run.
 */
UCLASS(BlueprintType)
class FPSRL_API UFPSRLRunDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Run")
	TArray<FFPSRLAreaEntry> Areas;

	/** Total Depths across all Areas. */
	int32 GetNumDepths() const;

	/** Flat index (0-based, across Areas) -> Depth, or null. OutAreaIndex / OutDepthInArea are 0-based. */
	const UFPSRLDepthDefinition* GetDepth(int32 FlatIndex, int32* OutAreaIndex = nullptr, int32* OutDepthInArea = nullptr) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Run"), GetFName()); }
};
