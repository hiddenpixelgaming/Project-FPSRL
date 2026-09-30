// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/FPSRLTypes.h"
#include "Data/FPSRLEnemyScaling.h"
#include "FPSRLEncounterDefinition.generated.h"

/**
 * What a room's encounter is: which enemy it spawns and how that enemy is scaled. Pure data (Primary Asset
 * "Encounter"), set on the room (AFPSRLRoom::Encounter). The expedition only cares about Kind (Normal / Elite /
 * FinalLevelBoss); swapping the placeholder shooter for a real Elite or Final Level Boss is a change to this asset only.
 *
 * Placeholders today: DA_Encounter_PlaceholderShooterElite (5x health) and DA_Encounter_PlaceholderShooterFinalBoss
 * (20x health), both the regular shooter enemy. No special mechanics are built on them.
 */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLEncounterDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shown on the health bar ("Elite", "Final Level Boss"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter")
	EFPSRLEncounterKind Kind = EFPSRLEncounterKind::Normal;

	/** Enemy spawned at the room's spawn points. Empty = the room's own EnemyClass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter")
	TSoftClassPtr<APawn> EnemyClass;

	/** Encounter-specific health modifier on top of the enemy's scaled health. The Elite (5x) and Final Level Boss (20x)
	 *  values are PROTOTYPE PLACEHOLDERS on the Normal Shooter: real Elites and bosses get their own UFPSRLEnemyDefinition
	 *  (base stats) and this goes back to 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.01"))
	float HealthMultiplier = 1.f;

	/** Encounter-specific damage modifier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	float DamageMultiplier = 1.f;

	/** Use ScalingOverride instead of the settings' enemy / boss player-count profile (e.g. a boss with its own curve). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter|Scaling")
	bool bOverrideScaling = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter|Scaling", meta = (EditCondition = "bOverrideScaling"))
	FFPSRLPlayerCountScaling ScalingOverride;

	/** The enemy's size times this (a placeholder cue that it's no ordinary enemy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.1"))
	float SizeMultiplier = 1.f;

	/** Show the Elite / boss health bar at the top of the screen while the encounter runs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Encounter")
	bool bShowHealthBar = false;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Encounter"), GetFName()); }
};
