// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "FPSRLBoonDefinition.generated.h"

class UTexture2D;

/**
 * One boon: a distinct, run-scoped gameplay upgrade (Abyssus-style). Boons have NO rarity - a boon is defined by
 * what it does, not by a tier. Pure data: one Data Asset per boon; its ID is the asset name (Primary Asset "Boon").
 *
 * Eligibility is data-driven through tags, so the Boon system never hardcodes combinations:
 *  - RequiredWeaponTags / RequiredAspectTags: the player's weapon / aspect must match at least one (empty = any).
 *  - RequiredElementTags: the player must already own a boon of at least one of these elements (empty = no requirement).
 *  - BlockedTags: excluded if the player carries any of these (weapon, aspect, build and element tags).
 *  - RequiredBoons: prerequisites that must all be owned.
 * Elements: ElementTags lists the element(s) this boon belongs to (0 = elementless). A player can own boons from at
 * most 3 elements; each owned boon of an element raises that element's offer weight (see UFPSRLBoonSettings).
 */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLBoonDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Presentation ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|UI")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|UI", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon", meta = (Categories = "Boon.Category"))
	FGameplayTag Category;

	/** This boon's own tags (Build.RapidFire, ...). Aspects can restrict boons by these. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon")
	FGameplayTagContainer BoonTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon", meta = (Categories = "Element"))
	FGameplayTagContainer ElementTags;

	// --- Eligibility ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility", meta = (Categories = "Weapon"))
	FGameplayTagContainer RequiredWeaponTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility", meta = (Categories = "Aspect"))
	FGameplayTagContainer RequiredAspectTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility", meta = (Categories = "Element"))
	FGameplayTagContainer RequiredElementTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility")
	FGameplayTagContainer BlockedTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility")
	TArray<TObjectPtr<UFPSRLBoonDefinition>> RequiredBoons;

	/** Base offer weight (no rarity: all boons start equal unless a designer tunes this). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Eligibility", meta = (ClampMin = "0"))
	float SelectionWeight = 1.f;

	// --- Stacking (per boon; there is no universal rule) ---
	/** Can be owned more than once. Each extra stack re-applies Grants.Effects (abilities/tags/cue come with the first). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Stacking")
	bool bCanStack = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Stacking", meta = (ClampMin = "1", EditCondition = "bCanStack"))
	int32 MaxStacks = 1;

	/** May be offered again once owned (only meaningful while stacks remain, or for boons shown for upgrade). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Stacking")
	bool bCanAppearAfterOwned = false;

	/** May appear in a rerolled set of choices (if false it only appears in a selection's first set). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon|Stacking")
	bool bCanReroll = true;

	/** Reserved for future explicitly team-wide boons. Standard boons are personal; not yet implemented. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon")
	bool bIsTeamBoon = false;

	// --- Effect ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boon")
	FFPSRLGrantSet Grants;

	int32 GetMaxStacks() const { return bCanStack ? FMath::Max(1, MaxStacks) : 1; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};

/** The boons that can be offered. Set in Project Settings > FPSRL Boons. */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLBoonPool : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boons")
	TArray<TObjectPtr<UFPSRLBoonDefinition>> Boons;
};
