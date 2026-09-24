// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRLBoonSettings.generated.h"

class UFPSRLAspectPool;
class UFPSRLBoonPool;

/**
 * Tuning for the Boon / Aspect / Element systems. Edit in Project Settings > Game > FPSRL Boons
 * (saved to Config/DefaultGame.ini). Every rule number lives here, not in gameplay code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Boons"))
class FPSRL_API UFPSRLBoonSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UFPSRLBoonSettings& Get() { return *GetDefault<UFPSRLBoonSettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Pools")
	TSoftObjectPtr<UFPSRLBoonPool> BoonPool;

	UPROPERTY(Config, EditAnywhere, Category = "Pools")
	TSoftObjectPtr<UFPSRLAspectPool> AspectPool;

	/** Boon choices per selection. */
	UPROPERTY(Config, EditAnywhere, Category = "Boon Selection", meta = (ClampMin = "1"))
	int32 BoonOptionsPerSelection = 3;

	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 FreeRerollsPerSelection = 3;

	/** Soul Fragments (Talent Essence) per reroll once the free ones are used. */
	UPROPERTY(Config, EditAnywhere, Category = "Rerolls", meta = (ClampMin = "0"))
	int32 RerollCost = 2;

	/** Distinct elements a player may own boons of during one run. */
	UPROPERTY(Config, EditAnywhere, Category = "Elements", meta = (ClampMin = "1"))
	int32 MaxElementsPerRun = 3;

	/** Offer-weight bonus per owned boon of an element (0.01 = +1% per boon). */
	UPROPERTY(Config, EditAnywhere, Category = "Elements", meta = (ClampMin = "0"))
	float ElementWeightBonusPerOwnedBoon = 0.01f;

	/** Aspect choices offered after picking a weapon (fewer if the pool has fewer for that weapon). */
	UPROPERTY(Config, EditAnywhere, Category = "Aspects", meta = (ClampMin = "1"))
	int32 AspectOptionsPerWeapon = 3;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
