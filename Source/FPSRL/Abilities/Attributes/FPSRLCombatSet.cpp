// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "Net/UnrealNetwork.h"

UFPSRLCombatSet::UFPSRLCombatSet()
	: DamageMultiplier(1.f)
	, RangedDamageMultiplier(1.f)
	, MeleeDamageMultiplier(1.f)
	, AbilityDamageMultiplier(1.f)
	, CritChance(0.f)
	, RangedCritChance(0.f)
	, MeleeCritChance(0.f)
	, CritDamageMultiplier(2.f)
	, FireRateMultiplier(1.f)
	, ReloadSpeedMultiplier(1.f)
	, MeleeCooldownMultiplier(1.f)
	, AbilityCooldownMultiplier(1.f)
	, MoveSpeedMultiplier(1.f)
{
}

void UFPSRLCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, DamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, RangedDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, MeleeDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, AbilityDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, CritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, RangedCritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, MeleeCritChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, CritDamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, FireRateMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, ReloadSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, MeleeCooldownMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, AbilityCooldownMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
}

void UFPSRLCombatSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetCritChanceAttribute() || Attribute == GetRangedCritChanceAttribute() || Attribute == GetMeleeCritChanceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
	}
	else
	{
		// Multipliers: never zero or negative, however many penalties stack (relic tradeoffs).
		NewValue = FMath::Max(NewValue, 0.1f);
	}
}

void UFPSRLCombatSet::OnRep_DamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, DamageMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_RangedDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, RangedDamageMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_MeleeDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, MeleeDamageMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_AbilityDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, AbilityDamageMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_CritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, CritChance, OldValue);
}

void UFPSRLCombatSet::OnRep_RangedCritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, RangedCritChance, OldValue);
}

void UFPSRLCombatSet::OnRep_MeleeCritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, MeleeCritChance, OldValue);
}

void UFPSRLCombatSet::OnRep_CritDamageMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, CritDamageMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, FireRateMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_ReloadSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, ReloadSpeedMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_MeleeCooldownMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, MeleeCooldownMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_AbilityCooldownMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, AbilityCooldownMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, MoveSpeedMultiplier, OldValue);
}
