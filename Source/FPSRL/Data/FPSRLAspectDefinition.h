// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "FPSRLAspectDefinition.generated.h"

class UTexture2D;

/**
 * An Aspect: the player's primary build identity for a run (e.g. Pistol -> Gunslinger). Chosen once, in the Lobby,
 * right after the weapon; locked for the whole run; cleared at run end. NOT rarity-based and not tied 1:1 to a
 * weapon (a weapon offers several aspects). Primary Asset type "Aspect".
 *
 * While active it grants AspectTag + Grants (effects, abilities, tags such as Build.RapidFire) through GAS, and it
 * shapes the boon pool: boons can require its tags (RequiredAspectTags) and it can exclude boons (RestrictedBoonTags).
 */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLAspectDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Weapon this aspect is offered for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Weapon"))
	FGameplayTag AssociatedWeapon;

	/** Extra weapon tags the player must have (usually empty; AssociatedWeapon already gates it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Weapon"))
	FGameplayTagContainer RequiredWeaponTags;

	/** Identity tag granted while active, e.g. Aspect.Gunslinger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Aspect"))
	FGameplayTag AspectTag;

	/** Elements this aspect is designed around (informational for now; for future synergy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Element"))
	FGameplayTagContainer ElementCompatibility;

	/** Boon tags that suit this aspect (stored for future offer biasing; no weighting rule defined yet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect")
	FGameplayTagContainer CompatibleBoonTags;

	/** Boons carrying any of these BoonTags are never offered to a player with this aspect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect")
	FGameplayTagContainer RestrictedBoonTags;

	/** Starting effects, abilities, extra tags and grant cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect")
	FFPSRLGrantSet Grants;

	/** True if this aspect can be offered to a player holding WeaponTag. */
	bool IsCompatibleWithWeapon(const FGameplayTag& WeaponTag) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};

/** The aspects that can be offered. Set in Project Settings > FPSRL Boons. */
UCLASS(BlueprintType, Const)
class FPSRL_API UFPSRLAspectPool : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspects")
	TArray<TObjectPtr<UFPSRLAspectDefinition>> Aspects;
};
