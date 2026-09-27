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

	/** Blessing choices per altar. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 BoonOptionsPerSelection = 3;

	/** Blessing positions per channel (a channel stops being offered once full). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 MaxBoonsPerChannel = 11;

	/** Position on a channel that is always its Aspect's Minor Blessing. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 MinorPosition = 3;

	/** Position on a channel that is always its Aspect's Major Blessing. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (ClampMin = "1"))
	int32 MajorPosition = 6;

	/** Secondary-channel item every player starts with (the built-in melee). */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (Categories = "Secondary"))
	FGameplayTag DefaultSecondaryItem;

	/** Ability-channel item every player starts with. Empty = no ability, so the channel is never offered. */
	UPROPERTY(Config, EditAnywhere, Category = "Blessings", meta = (Categories = "Ability"))
	FGameplayTag DefaultAbilityItem;

	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 FreeRerollsPerSelection = 3;

	/** Soul Fragments (Talent Essence) per reroll once the free ones are used. */
	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 RerollCost = 2;

	/** Choices on an Upgrade Altar (fewer if the player owns fewer upgradeable things). */
	UPROPERTY(Config, EditAnywhere, Category = "Upgrade Altar", meta = (ClampMin = "1"))
	int32 UpgradeOptionsPerSelection = 3;

	/** Relic drop odds by rarity (relative weights; rarities with no eligible relic are skipped). */
	UPROPERTY(Config, EditAnywhere, Category = "Relics")
	TMap<ERelicRarity, float> RelicRarityWeights;

	/** Channel position (1-based) -> its milestone type. */
	EFPSRLBoonType GetBoonTypeForPosition(int32 Position) const
	{
		return Position == MajorPosition ? EFPSRLBoonType::Major : Position == MinorPosition ? EFPSRLBoonType::Minor : EFPSRLBoonType::Normal;
	}

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
