// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "FPSRLGrantSet.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FFPSRLGrantedAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
	TSubclassOf<UGameplayAbility> Ability;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
	int32 Level = 1;
};

/**
 * What a boon or aspect gives a player through GAS while it is owned. Nothing here is a parallel stat system:
 * stat changes are Gameplay Effect assets (e.g. GE_Boon_RapidTrigger modifying CombatSet.FireRateMultiplier),
 * behavior is Gameplay Abilities, and identity is Gameplay Tags.
 */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLGrantSet
{
	GENERATED_BODY()

	/** Applied to the owner's ASC (usually Infinite duration). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
	TArray<TSubclassOf<UGameplayEffect>> Effects;

	/** Granted abilities, e.g. a passive reacting to Event.Hit or Event.Kill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
	TArray<FFPSRLGrantedAbility> Abilities;

	/** Tags the owner carries while this is owned (replicated), e.g. Aspect.Gunslinger, Build.RapidFire. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant")
	FGameplayTagContainer GrantedTags;

	/** Optional GameplayCue executed once when granted (presentation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grant", meta = (Categories = "GameplayCue"))
	FGameplayTag GrantCue;
};

/** Exactly what was given, so it can be taken back without touching anything else on the ASC. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLGrantHandles
{
	GENERATED_BODY()

	TArray<FActiveGameplayEffectHandle> EffectHandles;
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;
	FGameplayTagContainer LooseTags;
};

namespace FPSRLGrants
{
	/**
	 * Server-only. Applies the set to the ASC. Every effect spec gets DynamicAssetTags (always including
	 * Effect.Temporary.Run for run systems) so it can be identified later; SourceObject (the boon/aspect asset)
	 * is recorded on effects and abilities.
	 */
	FPSRL_API FFPSRLGrantHandles Give(const FFPSRLGrantSet& Set, UAbilitySystemComponent* ASC, UObject* SourceObject,
		const FGameplayTagContainer& DynamicAssetTags);

	/** Server-only. Removes exactly what Give added. Handles are reset. */
	FPSRL_API void Take(FFPSRLGrantHandles& Handles, UAbilitySystemComponent* ASC);
}
