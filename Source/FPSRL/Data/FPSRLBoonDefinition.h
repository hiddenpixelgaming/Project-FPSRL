// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLBoonDefinition.generated.h"

class UFPSRLAspectDefinition;
class UTexture2D;

/**
 * One Blessing (called a boon in code): a distinct run upgrade that belongs to an Aspect and attaches to a channel.
 * No rarity. Pure data: one asset per Blessing; its ID is the asset name (Primary Asset "Boon").
 *
 * Where it can be offered (all data, nothing hardcoded in the altar):
 *  - Aspect + AllowedChannels: only on a channel whose Aspect is this one (or an empty channel, as its first
 *    Blessing), and only on the channels listed (empty = any channel the Aspect allows).
 *  - SupportedSources: the kinds of attack it works with (Ranged, Melee, Ability); the channel's equipped item must be
 *    one of them. Empty = Universal. The same Aspect keeps one identity on any slot; its Blessings say what they fit.
 *  - BoonType: Normal, Minor or Major. A slot position offers one kind (BoonSettings.PositionTypes: 3rd = Minor, 6th =
 *    Major by default, the rest Normal).
 *  - RequiredItemTags: the channel's equipped item must carry one of them (e.g. Weapon.Cannon, Secondary.Melee,
 *    a future Ability.*). Empty = any item. This is how "needs a projectile weapon" / "needs melee" is expressed.
 *  - BlockedTags, RequiredBoons (prerequisites on the same channel), RequiredChannelCount.
 * Stacking: MaxStacks (1 = one-time). Upgrading (Upgrade Altar): each upgrade level adds UpgradedGrants on top, so
 * upgrades stack, up to MaxUpgradeLevel.
 */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLBoonDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Presentation ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|UI")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|UI", meta = (MultiLine = "true"))
	FText Description;

	/** What one upgrade does, shown on the Upgrade Altar (each level adds it again). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|UI", meta = (MultiLine = "true"))
	FText UpgradeDescription;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing", meta = (Categories = "Boon.Category"))
	FGameplayTag Category;

	/** This Blessing's own tags (Build.RapidFire, ...), for synergies and BlockedTags on other Blessings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	FGameplayTagContainer BoonTags;

	// --- Family and channel ---
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	TObjectPtr<UFPSRLAspectDefinition> Aspect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	EFPSRLBoonType BoonType = EFPSRLBoonType::Normal;

	/** Kinds of attack this Blessing works with. Empty = Universal (any source). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility")
	TArray<EFPSRLItemSource> SupportedSources;

	/** Works with this source (true for every source when SupportedSources is empty). */
	bool SupportsSource(EFPSRLItemSource Source) const { return SupportedSources.IsEmpty() || SupportedSources.Contains(Source); }

	/** Channels it can attach to. Empty = any channel its Aspect allows. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	TArray<EFPSRLBoonChannel> AllowedChannels;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	EFPSRLBoonScope Scope = EFPSRLBoonScope::Self;

	// --- Eligibility ---
	/** The channel's equipped item must have one of these (Weapon.*, Secondary.*, Ability.*). Empty = any. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility")
	FGameplayTagContainer RequiredItemTags;

	/** Not offered while the player carries any of these (on their ASC or owned Blessings' BoonTags). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility")
	FGameplayTagContainer BlockedTags;

	/** Must already be owned on the same channel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility")
	TArray<TObjectPtr<UFPSRLBoonDefinition>> RequiredBoons;

	/** Minimum Blessings already on the channel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility", meta = (ClampMin = "0"))
	int32 RequiredChannelCount = 0;

	/** Relative offer weight. 0 = never offered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility", meta = (ClampMin = "0"))
	float SelectionWeight = 1.f;

	/** May appear in a rerolled set (if false, only in an altar's first set). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Eligibility")
	bool bCanReroll = true;

	// --- Stacking and upgrade ---
	/** Times it can be taken (1 = one-time). Each stack counts as one Blessing on the channel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Stacking", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** Times the Upgrade Altar can improve it; upgrades stack (0 = never offered). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing|Stacking", meta = (ClampMin = "0"))
	int32 MaxUpgradeLevel = 10;

	// --- Effect ---
	/** Applied once per stack (abilities, tags and cue only with the first). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	FFPSRLGrantSet Grants;

	/** Added on top of Grants once per upgrade level (per stack; abilities, tags and cue only with the first). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessing")
	FFPSRLGrantSet UpgradedGrants;

	int32 GetMaxStacks() const { return FMath::Max(1, MaxStacks); }

	/** Can attach to this channel (its own list and its Aspect's). */
	bool AllowsChannel(EFPSRLBoonChannel Channel) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};

/** The Blessings altars can offer. Set in Project Settings > FPSRL Blessings. */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLBoonPool : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blessings")
	TArray<TObjectPtr<UFPSRLBoonDefinition>> Boons;
};
