// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLGrantSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

namespace FPSRLGrants
{
	FFPSRLGrantHandles Give(const FFPSRLGrantSet& Set, UAbilitySystemComponent* ASC, UObject* SourceObject,
		const FGameplayTagContainer& DynamicAssetTags)
	{
		FFPSRLGrantHandles Handles;
		if (!ASC || !ASC->IsOwnerActorAuthoritative())
		{
			return Handles;
		}

		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(SourceObject);

		for (const TSubclassOf<UGameplayEffect>& EffectClass : Set.Effects)
		{
			if (!EffectClass)
			{
				continue;
			}
			const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
			if (Spec.IsValid())
			{
				Spec.Data->AppendDynamicAssetTags(DynamicAssetTags);
				Handles.EffectHandles.Add(ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data));
			}
		}

		for (const FFPSRLGrantedAbility& Granted : Set.Abilities)
		{
			if (Granted.Ability)
			{
				FGameplayAbilitySpec AbilitySpec(Granted.Ability, Granted.Level, INDEX_NONE, SourceObject);
				Handles.AbilityHandles.Add(ASC->GiveAbility(AbilitySpec));
			}
		}

		for (const FGameplayTag& Tag : Set.GrantedTags)
		{
			ASC->AddLooseGameplayTag(Tag, 1, EGameplayTagReplicationState::TagOnly);
		}
		Handles.LooseTags = Set.GrantedTags;

		if (Set.GrantCue.IsValid())
		{
			ASC->ExecuteGameplayCue(Set.GrantCue, Context);
		}

		return Handles;
	}

	void Take(FFPSRLGrantHandles& Handles, UAbilitySystemComponent* ASC)
	{
		if (ASC && ASC->IsOwnerActorAuthoritative())
		{
			for (const FActiveGameplayEffectHandle& Handle : Handles.EffectHandles)
			{
				if (Handle.IsValid())
				{
					ASC->RemoveActiveGameplayEffect(Handle);
				}
			}
			for (const FGameplayAbilitySpecHandle& Handle : Handles.AbilityHandles)
			{
				if (Handle.IsValid())
				{
					ASC->ClearAbility(Handle);
				}
			}
			for (const FGameplayTag& Tag : Handles.LooseTags)
			{
				ASC->RemoveLooseGameplayTag(Tag, 1, EGameplayTagReplicationState::TagOnly);
			}
		}
		Handles = FFPSRLGrantHandles();
	}
}
