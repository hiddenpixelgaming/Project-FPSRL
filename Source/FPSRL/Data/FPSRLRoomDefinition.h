// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRoomDefinition.generated.h"

/**
 * One handcrafted room level that Depths can be assembled from (room library entry).
 *
 * The level is authored with its entrance at the origin facing +X, and an AFPSRLRoomConnector at its exit.
 * ExitTransform is that connector's transform; press "Bake Exit From Level" after editing the level so the generator
 * can place the following room without loading this one first.
 *
 * What the room contains (enemies, an AFPSRLRoom, a Boon altar, a merchant, the exit portal) lives in the level itself.
 */
UCLASS(BlueprintType)
class FPSRL_API UFPSRLRoomDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Room")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Room")
	ERoomType RoomType = ERoomType::Combat;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Room")
	TSoftObjectPtr<UWorld> Level;

	/** Combat arenas: the gameplay identity (metadata for selection, tools and future encounter rules). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	EFPSRLArenaType ArenaType = EFPSRLArenaType::None;

	/** Depths of an Area this room may appear in (1-based Depth within its Area; 0 = no limit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "0"))
	int32 MinDepth = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "0"))
	int32 MaxDepth = 0;

	/** Allowed in this Depth (1-based within its Area; 0 = unknown: allowed). */
	bool IsEligibleForDepth(int32 DepthInArea) const
	{
		return DepthInArea <= 0 || ((MinDepth <= 0 || DepthInArea >= MinDepth) && (MaxDepth <= 0 || DepthInArea <= MaxDepth));
	}

	/** Relative odds of being picked from its pool. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Room", meta = (ClampMin = "0"))
	float SelectionWeight = 1.f;

	/** Where the next room attaches, relative to this level's origin (baked from its AFPSRLRoomConnector). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Room")
	FTransform ExitTransform;

	/** Editor: read ExitTransform from the level's AFPSRLRoomConnector. */
	UFUNCTION(CallInEditor, Category = "Room")
	void BakeExitFromLevel();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Room"), GetFName()); }
};
