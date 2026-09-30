// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Effects/FPSRLStatusEffects.h"
#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "NativeGameplayTags.h"
#include "Types/FPSRLGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Test_Marked, "Test.Marked");

UFPSRLPeriodicDamageEffect::UFPSRLPeriodicDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.f));
	Period = FScalableFloat(1.f);
	bExecutePeriodicEffectOnApplication = false;

	FSetByCallerFloat Damage;
	Damage.DataTag = FPSRLGameplayTags::SetByCaller_Damage;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFPSRLHealthSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Damage);
	Modifiers.Add(Modifier);
}

UFPSRLTestMarkEffect::UFPSRLTestMarkEffect()
{
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	FInheritedTagContainer Tags;
	Tags.AddTag(TAG_Test_Marked);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
	GEComponents.Add(TargetTags);
}

UFPSRLTestRangedDamageEffect::UFPSRLTestRangedDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UFPSRLCombatSet::GetRangedDamageMultiplierAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(0.5f));
	Modifiers.Add(Modifier);
}

namespace
{
	void AddInfiniteModifier(UGameplayEffect* Effect, const FGameplayAttribute& Attribute, float Value)
	{
		Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Value));
		Effect->Modifiers.Add(Modifier);
	}
}

UFPSRLTestFireRateEffect::UFPSRLTestFireRateEffect()
{
	AddInfiniteModifier(this, UFPSRLCombatSet::GetFireRateMultiplierAttribute(), 1.f);
}

UFPSRLTestMeleeSpeedEffect::UFPSRLTestMeleeSpeedEffect()
{
	AddInfiniteModifier(this, UFPSRLCombatSet::GetMeleeCooldownMultiplierAttribute(), -2.f / 3.f);
}
