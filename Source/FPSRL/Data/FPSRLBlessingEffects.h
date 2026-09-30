// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "FPSRLBlessingEffects.generated.h"

class UGameplayEffect;

/**
 * What a Blessing does beyond its always-on Grants (stat Gameplay Effects, abilities, tags): data that reacts to combat.
 * Everything runs on the server through GAS (Gameplay Effects applied with the attacker's ability system).
 *
 * A Blessing reacts only to hits from its own slot's source (Fire on the Primary gun reacts to Ranged hits, Fire on the
 * Secondary melee to Melee hits), so the same Aspect stays one identity with source-appropriate behaviour.
 * Magnitudes grow per upgrade level and multiply by stacks.
 */

/** Who an action affects. Areas only ever hit enemies (never players). */
UENUM(BlueprintType)
enum class EFPSRLBlessingActionTarget : uint8
{
	HitTarget,			// the enemy that was hit / killed
	Self,				// the player who owns the Blessing
	AreaAroundTarget,	// every enemy within Radius of the hit enemy (the hit enemy included)
	AreaAroundSelf		// every enemy within Radius of the player
};

/** One thing a trigger does: apply a Gameplay Effect (a status like Burn, damage, healing, a buff...). */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLBlessingAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	EFPSRLBlessingActionTarget Target = EFPSRLBlessingActionTarget::HitTarget;

	/** Area actions: radius in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (ClampMin = "0"))
	float Radius = 300.f;

	/** The effect applied (its level = the Blessing's upgrade level + 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	TSubclassOf<UGameplayEffect> Effect;

	/** Set-by-caller magnitude handed to the effect (e.g. SetByCaller.Damage). Empty = the effect needs none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (Categories = "SetByCaller"))
	FGameplayTag MagnitudeTag;

	/** Magnitude = (Magnitude + MagnitudePerUpgrade x upgrade level + HitDamageFraction x the hit's damage) x stacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	float Magnitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	float MagnitudePerUpgrade = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	float HitDamageFraction = 0.f;
};

/** When something happens (Event.Hit, Event.Kill, ...) and the conditions pass, run the actions. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLBlessingTrigger
{
	GENERATED_BODY()

	/** The combat event (Event.Hit, Event.Kill, or any other event sent to the player's ability system). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (Categories = "Event"))
	FGameplayTag Event;

	/** Only on critical hits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	bool bCriticalOnly = false;

	/** Chance per qualifying event (0..1), plus this per upgrade level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (ClampMin = "0", ClampMax = "1"))
	float Chance = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (ClampMin = "0"))
	float ChancePerUpgrade = 0.f;

	/** Only every Nth qualifying event (1 = every one). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing", meta = (ClampMin = "1"))
	int32 EveryNth = 1;

	/** Tags the hit enemy must / must not have (e.g. require Status.Burning). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	FGameplayTagRequirements TargetRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	TArray<FFPSRLBlessingAction> Actions;
};

/** Extra damage on this slot's hits when the conditions pass (e.g. +50% against burning enemies). */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLBlessingDamageBonus
{
	GENERATED_BODY()

	/** Tags the hit enemy must / must not have. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	FGameplayTagRequirements TargetRequirements;

	/** Only on critical hits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	bool bCriticalOnly = false;

	/** Added damage share (0.5 = +50%), plus this per upgrade level; multiplied by stacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	float Bonus = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blessing")
	float BonusPerUpgrade = 0.f;
};
