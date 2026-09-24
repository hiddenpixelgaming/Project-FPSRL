// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "Net/UnrealNetwork.h"

UFPSRLCombatSet::UFPSRLCombatSet()
	: DamageMultiplier(1.f)
	, FireRateMultiplier(1.f)
	, ReloadSpeedMultiplier(1.f)
	, MoveSpeedMultiplier(1.f)
	, CritChance(0.f)
{
}

void UFPSRLCombatSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, DamageMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, FireRateMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, ReloadSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLCombatSet, CritChance, COND_None, REPNOTIFY_Always);
}

void UFPSRLCombatSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetCritChanceAttribute())
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

void UFPSRLCombatSet::OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, FireRateMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_ReloadSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, ReloadSpeedMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, MoveSpeedMultiplier, OldValue);
}

void UFPSRLCombatSet::OnRep_CritChance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLCombatSet, CritChance, OldValue);
}
