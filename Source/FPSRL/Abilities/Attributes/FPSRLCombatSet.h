// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "FPSRLCombatSet.generated.h"

/**
 * Player combat stats that boons, relics and talents modify (via Gameplay Effects, never by setting them directly).
 * Multipliers start at 1.0; a +10% boon adds 0.1. GAS sums additive modifiers before applying them, so two +10%
 * boons give 1.2, not 1.21 - balance with that in mind.
 *
 * Replicated so the HUD and client-side feel (fire rate, movement) can read them. Server is authoritative.
 * Consumers: damage (DamageMultiplier, CritChance) and weapon/movement code read these when they move to C++
 * (Step F); until then the values exist and stack correctly but nothing reads them yet.
 */
UCLASS()
class FPSRL_API UFPSRLCombatSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRLCombatSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, DamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, FireRateMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, ReloadSpeedMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, MoveSpeedMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, CritChance)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

protected:
	UFUNCTION() void OnRep_DamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ReloadSpeedMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritChance(const FGameplayAttributeData& OldValue);

private:
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData DamageMultiplier;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FireRateMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData FireRateMultiplier;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReloadSpeedMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData ReloadSpeedMultiplier;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MoveSpeedMultiplier;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritChance, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData CritChance;
};
