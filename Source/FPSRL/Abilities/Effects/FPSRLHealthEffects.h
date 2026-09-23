// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FPSRLHealthEffects.generated.h"

/**
 * Instant damage. Writes SetByCaller.Damage into UFPSRLHealthSet's Damage meta attribute.
 * Defined in C++ (not as an asset) because every damage source uses it and its shape never changes;
 * variation comes from the magnitude and from tags on the spec (Damage.Bullet, Damage.Explosive, ...).
 */
UCLASS()
class FPSRL_API UFPSRLDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLDamageEffect();
};

/** Instant heal. Writes SetByCaller.Healing into UFPSRLHealthSet's Healing meta attribute. */
UCLASS()
class FPSRL_API UFPSRLHealEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UFPSRLHealEffect();
};
