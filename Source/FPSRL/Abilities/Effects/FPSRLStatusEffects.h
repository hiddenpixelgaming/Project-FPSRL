// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FPSRLStatusEffects.generated.h"

/**
 * Building block for damage over time (burn, poison, bleed...): every Period seconds for Duration, it deals the
 * SetByCaller.Damage magnitude the Blessing action hands it. Make a Blueprint child per status to set its duration,
 * period, stacking and the status tag it grants the target (Target Tags component, e.g. Status.Burning), which other
 * Blessings can test in their conditions.
 */
UCLASS(Blueprintable)
class FPSRL_API UFPSRLPeriodicDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLPeriodicDamageEffect();
};

/** TEST ONLY (framework checks, not content): a 3 s, 1 s-tick burn that tags its target Test.Marked. */
UCLASS(NotBlueprintable)
class FPSRL_API UFPSRLTestMarkEffect : public UFPSRLPeriodicDamageEffect
{
	GENERATED_BODY()

public:
	UFPSRLTestMarkEffect();
};

/** TEST ONLY (framework checks, not content): +0.5 RangedDamageMultiplier while applied. */
UCLASS(NotBlueprintable)
class FPSRL_API UFPSRLTestRangedDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLTestRangedDamageEffect();
};

/** TEST ONLY (proc checks): +1 FireRateMultiplier (double fire rate) while applied. */
UCLASS(NotBlueprintable)
class FPSRL_API UFPSRLTestFireRateEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLTestFireRateEffect();
};

/** TEST ONLY (proc checks): MeleeCooldownMultiplier -2/3 (three times the melee speed) while applied. */
UCLASS(NotBlueprintable)
class FPSRL_API UFPSRLTestMeleeSpeedEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLTestMeleeSpeedEffect();
};
