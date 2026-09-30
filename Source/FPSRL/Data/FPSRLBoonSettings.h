// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLBoonSettings.generated.h"

class UFPSRLBoonPool;
class UFPSRLRelicPool;

/**
 * Tuning for Blessings (boons), Aspects, the Upgrade Altar and Relics. Edit in Project Settings > Game > FPSRL
 * Blessings (saved to Config/DefaultGame.ini). Every rule number lives here, not in gameplay code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Blessings"))
class FPSRL_API UFPSRLBoonSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFPSRLBoonSettings();

	static const UFPSRLBoonSettings& Get() { return *GetDefault<UFPSRLBoonSettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Pools")
	TSoftObjectPtr<UFPSRLBoonPool> BoonPool;

	UPROPERTY(Config, EditAnywhere, Category = "Pools")
	TSoftObjectPtr<UFPSRLRelicPool> RelicPool;

	/** Aspect choices at the first step of a Blessing altar. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 AspectOptionsPerAltar = 3;

	/** How much likelier an Aspect already on one of the player's slots is offered than a new one (1 = no preference). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "0"))
	float AssignedAspectWeight = 3.f;

	/** On a reroll, how likely the new Aspects just shown are to come back (1 = as likely as any, 0 = never). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "0", ClampMax = "1"))
	float RerollRepeatWeight = 0.25f;

	/** Blessing choices after an Aspect (and slot) is picked. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 BoonOptionsPerSelection = 2;

	/** Blessing positions per channel (a channel stops being offered once full). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 MaxBoonsPerChannel = 11;

	/** Which kind of Blessing a slot position offers (1 = a slot's first Blessing). Positions not listed offer Normal
	 *  Blessings. Default: the 3rd is a Minor, the 6th a Major. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings")
	TMap<int32, EFPSRLBoonType> PositionTypes = { { 3, EFPSRLBoonType::Minor }, { 6, EFPSRLBoonType::Major } };

	/** Blessing kind offered at a slot position (1-based). */
	EFPSRLBoonType GetTypeForPosition(int32 Position) const { const EFPSRLBoonType* Type = PositionTypes.Find(Position); return Type ? *Type : EFPSRLBoonType::Normal; }

	/** What each equipped item is, for Blessing source compatibility (matched by tag, parents included: Weapon covers
	 *  Weapon.Rifle). An item not listed takes its slot's default: Primary Ranged, Secondary Melee, Ability Ability. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings")
	TMap<FGameplayTag, EFPSRLItemSource> ItemSources;

	/** Secondary-channel item every player starts with (the built-in melee). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (Categories = "Secondary"))
	FGameplayTag DefaultSecondaryItem;

	/** Ability-channel item every player starts with. Empty = no ability, so the channel is never offered. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (Categories = "Ability"))
	FGameplayTag DefaultAbilityItem;

	/** Free rerolls per player per RUN (not per altar): once used they only come back when the run ends. */
	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 FreeRerollsPerRun = 3;

	/** Soul Fragments (Talent Essence) per reroll once the run's free ones are used. */
	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 RerollCost = 2;

	/** Choices on an Upgrade Altar (fewer if the player owns fewer upgradeable things). */
	UPROPERTY(Config, EditAnywhere, Category = "Upgrade Altar", meta = (ClampMin = "1"))
	int32 UpgradeOptionsPerSelection = 3;

	/** Relic drop odds by rarity (relative weights; rarities with no eligible relic are skipped). */
	UPROPERTY(Config, EditAnywhere, Category = "Relics")
	TMap<ERelicRarity, float> RelicRarityWeights;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
