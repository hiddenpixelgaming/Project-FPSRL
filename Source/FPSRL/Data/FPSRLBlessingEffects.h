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

/**
 * How a Blessing's proc decides to fire. Each Blessing picks the model its reference behaviour uses; there is no global
 * default (not everything is "chance per hit", not everything is normalized).
 */
UENUM(BlueprintType)
enum class EFPSRLProcModel : uint8
{
	/** Chance per eligible event (Chance + ChancePerUpgrade). Faster weapons / more pellets proc more often. */
	ChancePerEvent,
	/** Normalized frequency: chance = ProcsPerSecond x the source's CURRENT event interval (fire rate, melee speed...),
	 *  clamped to MinChance..MaxChance, so a fast and a slow weapon proc about equally often per second. */
	Normalized,
	/** Accumulation: each eligible event adds 1 (or its damage) to a per-player counter; at Threshold it procs and resets. */
	Accumulate,
	/** Periodic: procs on its own every Interval seconds while owned (a timer, no events, no Tick). */
	Periodic
};

/** What one eligible event is. */
UENUM(BlueprintType)
enum class EFPSRLProcScope : uint8
{
	/** Every event counts: each pellet / projectile / melee target hit is its own roll. */
	EachEvent,
	/** Once per attack: the first event of an attack is evaluated, the rest of that attack (other pellets, cleave
	 *  targets) are ignored. A shotgun blast = one roll. */
	OncePerAttack
};

/** Accumulate model: what the counter adds up. */
UENUM(BlueprintType)
enum class EFPSRLProcAccumulation : uint8
{
	Events,		// +1 per eligible event
	Damage		// + the event's damage (Event.Hit / Event.Kill)
};

/**
 * One proc of a Blessing (its proc definition). The Blessing owns it; the state (counters, cooldown) is kept per player
 * per owned Blessing on the server. Events reach it from that Blessing's own slot source only.
 *
 *   Event (Event.Attack / Event.Hit / Event.Kill / any Event.*) -> scope (each event / once per attack) -> conditions
 *   (critical, min damage, target tags) -> internal cooldown -> model (chance / normalized / accumulate / periodic)
 *   -> server roll -> actions (Gameplay Effects).
 *
 * Event.Attack = an attack was made (a trigger pull, a melee swing, an ability cast), hit or miss; Event.Hit = damage
 * landed (one per pellet / target); Event.Kill = the hit killed.
 */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLBlessingTrigger
{
	GENERATED_BODY()

	/** The event it reacts to (ignored by the Periodic model). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (Categories = "Event"))
	FGameplayTag Event;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc")
	EFPSRLProcModel Model = EFPSRLProcModel::ChancePerEvent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc")
	EFPSRLProcScope Scope = EFPSRLProcScope::EachEvent;

	/** ChancePerEvent: chance per eligible event (0..1), plus this per upgrade level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Model == EFPSRLProcModel::ChancePerEvent"))
	float Chance = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", EditCondition = "Model == EFPSRLProcModel::ChancePerEvent"))
	float ChancePerUpgrade = 0.f;

	/** Normalized: target procs per second (plus this per upgrade level). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", EditCondition = "Model == EFPSRLProcModel::Normalized"))
	float ProcsPerSecond = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", EditCondition = "Model == EFPSRLProcModel::Normalized"))
	float ProcsPerSecondPerUpgrade = 0.f;

	/** Normalized: limits on the per-event chance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Model == EFPSRLProcModel::Normalized"))
	float MinChance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Model == EFPSRLProcModel::Normalized"))
	float MaxChance = 1.f;

	/** Normalized: event interval (s) to assume when the source has no measurable rate (e.g. no ability cooldown yet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0.01", EditCondition = "Model == EFPSRLProcModel::Normalized"))
	float FallbackInterval = 1.f;

	/** Accumulate: procs when the counter reaches this (stacks, hits, damage...), then resets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0", EditCondition = "Model == EFPSRLProcModel::Accumulate"))
	float Threshold = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (EditCondition = "Model == EFPSRLProcModel::Accumulate"))
	EFPSRLProcAccumulation AccumulateBy = EFPSRLProcAccumulation::Events;

	/** Periodic: seconds between procs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0.05", EditCondition = "Model == EFPSRLProcModel::Periodic"))
	float Interval = 1.f;

	/** Internal cooldown (s): after a proc, eligible events are ignored for this long (0 = none). Any model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc", meta = (ClampMin = "0"))
	float InternalCooldown = 0.f;

	/** Conditions: only on critical hits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc|Conditions")
	bool bCriticalOnly = false;

	/** Conditions: the event's damage must be at least this (Event.Hit / Event.Kill; 0 = any). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc|Conditions", meta = (ClampMin = "0"))
	float MinDamage = 0.f;

	/** Conditions: tags the hit enemy must / must not have (e.g. require Status.Burning). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc|Conditions")
	FGameplayTagRequirements TargetRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Proc")
	TArray<FFPSRLBlessingAction> Actions;

	/** Design note: the reference behaviour this reproduces (Trigger / Proc model / Cooldown / Frequency / Source, and
	 *  whether it is verified or a project balancing parameter). */
	UPROPERTY(EditAnywhere, Category = "Proc", meta = (MultiLine = "true"))
	FString ReferenceBehaviour;
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
