// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "FPSRLCombatSet.generated.h"

/**
 * Player combat stats that Blessings, relics and talents modify (via Gameplay Effects, never by setting them directly).
 * Multipliers start at 1.0; a +10% Blessing adds 0.1. GAS sums additive modifiers before applying them, so two +10%
 * Blessings give 1.2, not 1.21 - balance with that in mind.
 *
 * Source-scoped where it matters (Ranged / Melee / Ability), so a Blessing on the Primary gun doesn't boost melee:
 * outgoing damage = base x DamageMultiplier x <Source>DamageMultiplier (x CritDamageMultiplier on a critical, whose
 * chance is CritChance + <Source>CritChance). Read by UFPSRLHealthComponent (damage), AFPSRLWeapon (fire rate, reload),
 * AFPSRLPlayerController (melee cooldown) and AFPSRLPlayerState (move speed).
 *
 * Replicated so the HUD and client-side feel (fire rate, movement) can read them. Server is authoritative.
 */
UCLASS()
class FPSRL_API UFPSRLCombatSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRLCombatSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, DamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, RangedDamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, MeleeDamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, AbilityDamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, CritChance)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, RangedCritChance)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, MeleeCritChance)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, CritDamageMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, FireRateMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, ReloadSpeedMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, MeleeCooldownMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, AbilityCooldownMultiplier)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLCombatSet, MoveSpeedMultiplier)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

protected:
	UFUNCTION() void OnRep_DamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_RangedDamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MeleeDamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AbilityDamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_RangedCritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MeleeCritChance(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_CritDamageMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_ReloadSpeedMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MeleeCooldownMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AbilityCooldownMultiplier(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);

private:
	/** All damage the player deals (every source). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData DamageMultiplier;

	/** Damage from Ranged sources (guns, projectiles). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RangedDamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData RangedDamageMultiplier;

	/** Damage from Melee sources. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MeleeDamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MeleeDamageMultiplier;

	/** Damage from Ability sources. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AbilityDamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData AbilityDamageMultiplier;

	/** Critical hit chance for every source (0..1). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritChance, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData CritChance;

	/** Extra critical chance for Ranged hits (0..1). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RangedCritChance, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData RangedCritChance;

	/** Extra critical chance for Melee hits (0..1). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MeleeCritChance, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MeleeCritChance;

	/** Damage multiplier of a critical hit (2 = double). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritDamageMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData CritDamageMultiplier;

	/** Ranged: shots per second (2 = twice as fast). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FireRateMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData FireRateMultiplier;

	/** Ranged: reload speed (2 = half the reload time). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReloadSpeedMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData ReloadSpeedMultiplier;

	/** Melee: cooldown length (0.5 = half the cooldown). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MeleeCooldownMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MeleeCooldownMultiplier;

	/** Ability: cooldown length (0.5 = half the cooldown). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AbilityCooldownMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData AbilityCooldownMultiplier;

	/** Movement speed. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedMultiplier, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MoveSpeedMultiplier;

};
