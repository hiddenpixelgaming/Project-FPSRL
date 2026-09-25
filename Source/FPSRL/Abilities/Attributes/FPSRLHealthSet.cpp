// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"

UFPSRLHealthSet::UFPSRLHealthSet()
	: Health(100.f)
	, MaxHealth(100.f)
	, Damage(0.f)
	, Healing(0.f)
{
}

void UFPSRLHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// REPNOTIFY_Always: clients must hear every change, even if a predicted local value already matches.
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLHealthSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLHealthSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

bool UFPSRLHealthSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}

	// Veto damage while invulnerable (e.g. boss phase transitions) or already dead.
	if (Data.EvaluatedData.Attribute == GetDamageAttribute() && Data.EvaluatedData.Magnitude > 0.f)
	{
		if (bOutOfHealth || Data.Target.HasMatchingGameplayTag(FPSRLGameplayTags::Status_Invulnerable))
		{
			Data.EvaluatedData.Magnitude = 0.f;
			return false;
		}
	}

	return true;
}

void UFPSRLHealthSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float IncomingDamage = GetDamage();
		SetDamage(0.f);
		const float NewHealth = GetHealth() - IncomingDamage;

		// A downable player (in a run) is downed instead of killed: health stays at 1, so nothing anywhere sees
		// them as dead, and the owner decides what happens next (down, or die if nobody can revive them).
		if (NewHealth <= 0.f && Data.Target.HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downable)
			&& !Data.Target.HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downed))
		{
			SetHealth(FMath::Min(1.f, GetMaxHealth()));
			const FGameplayEffectContextHandle& Context = Data.EffectSpec.GetEffectContext();
			OnDowned.Broadcast(Context.GetOriginalInstigator(), Context.GetEffectCauser());
			return;
		}
		SetHealth(FMath::Clamp(NewHealth, 0.f, GetMaxHealth()));
	}
	else if (Data.EvaluatedData.Attribute == GetHealingAttribute())
	{
		const float IncomingHealing = GetHealing();
		SetHealing(0.f);
		SetHealth(FMath::Clamp(GetHealth() + IncomingHealing, 0.f, GetMaxHealth()));
	}

	if (GetHealth() <= 0.f && !bOutOfHealth)
	{
		bOutOfHealth = true;

		const FGameplayEffectContextHandle& Context = Data.EffectSpec.GetEffectContext();
		OnOutOfHealth.Broadcast(Context.GetOriginalInstigator(), Context.GetEffectCauser());
	}
}

void UFPSRLHealthSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRLHealthSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRLHealthSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetMaxHealthAttribute())
	{
		// Lowering MaxHealth (e.g. a relic tradeoff) must not leave Health above the new cap.
		if (GetHealth() > NewValue)
		{
			if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
			{
				ASC->ApplyModToAttribute(GetHealthAttribute(), EGameplayModOp::Override, NewValue);
			}
		}
	}
	else if (Attribute == GetHealthAttribute() && NewValue > 0.f)
	{
		bOutOfHealth = false;
	}
}

void UFPSRLHealthSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f);
	}
}

void UFPSRLHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLHealthSet, Health, OldValue);
}

void UFPSRLHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLHealthSet, MaxHealth, OldValue);
}
