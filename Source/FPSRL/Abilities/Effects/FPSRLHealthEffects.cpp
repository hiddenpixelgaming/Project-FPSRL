// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Effects/FPSRLHealthEffects.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Types/FPSRLGameplayTags.h"

namespace
{
	/** One instant, additive modifier on Attribute whose magnitude is supplied at apply time via DataTag. */
	FGameplayModifierInfo MakeSetByCallerModifier(const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		return Modifier;
	}
}

UFPSRLDamageEffect::UFPSRLDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(UFPSRLHealthSet::GetDamageAttribute(), FPSRLGameplayTags::SetByCaller_Damage));
}

UFPSRLHealEffect::UFPSRLHealEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(MakeSetByCallerModifier(UFPSRLHealthSet::GetHealingAttribute(), FPSRLGameplayTags::SetByCaller_Healing));
}
