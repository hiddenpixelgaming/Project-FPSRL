// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRelicDefinition.generated.h"

class UTexture2D;

/**
 * One Relic: a run-level modifier, separate from Blessings. It never occupies a channel or counts toward an Aspect.
 * Has a rarity (drop odds in Project Settings > FPSRL Blessings). One asset per Relic (Primary Asset "Relic").
 * The player's owned relics and stacks live on UFPSRLRelicComponent, never on this asset.
 */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLRelicDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|UI")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|UI", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic")
	ERelicRarity Rarity = ERelicRarity::Normal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic")
	FGameplayTagContainer RelicTags;

	/** Times it can be owned (1 = one-time). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** At most one relic per group (e.g. Relic.Group.Core). Empty = no group. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|Eligibility")
	FGameplayTag ExclusivityGroup;

	/** The player must carry one of these (ASC tags). Empty = no requirement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|Eligibility")
	FGameplayTagContainer RequiredTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|Eligibility")
	FGameplayTagContainer BlockedTags;

	/** Relative weight within its rarity. 0 = never dropped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic|Eligibility", meta = (ClampMin = "0"))
	float SelectionWeight = 1.f;

	/** Applied once per stack (abilities, tags and cue only with the first). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relic")
	FFPSRLGrantSet Grants;

	int32 GetMaxStacks() const { return FMath::Max(1, MaxStacks); }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Relic"), GetFName()); }
};

/** The relics that can drop. Set in Project Settings > FPSRL Blessings. */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLRelicPool : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Relics")
	TArray<TObjectPtr<UFPSRLRelicDefinition>> Relics;
};
