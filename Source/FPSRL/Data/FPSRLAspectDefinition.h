// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLAspectDefinition.generated.h"

class UTexture2D;

/** What having this Aspect on one channel gives the player, before and after an Upgrade Altar improves it. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLAspectChannelGrants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aspect")
	EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;

	/** The Aspect's own mechanic on this channel, granted with the channel's first Blessing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aspect")
	FFPSRLGrantSet Grants;

	/** Replaces Grants once the Aspect is upgraded. Empty = the upgrade keeps Grants as they are. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aspect")
	FFPSRLGrantSet UpgradedGrants;
};

/**
 * An Aspect: a Blessing family (Air, Fire, Water, Earth, Light, Dark). Pure data, one asset per Aspect
 * (Primary Asset "Aspect"); Blessings name their Aspect (UFPSRLBoonDefinition::Aspect).
 *
 * A player's channels (Primary / Secondary / Ability) each hold at most one Aspect, set by the channel's first
 * Blessing. The same Aspect may sit on several channels; each one is a separate progression with its own count,
 * milestones and upgrade level (see UFPSRLBoonComponent).
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

	/** Shown on the Upgrade Altar for this Aspect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI", meta = (MultiLine = "true"))
	FText UpgradeDescription;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Identity tag, granted while a channel holds this Aspect (Aspect.Fire, ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Aspect"))
	FGameplayTag AspectTag;

	/** The element this Aspect's damage and statuses belong to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (Categories = "Element"))
	FGameplayTag ElementTag;

	/** Channels this Aspect can be taken on. Empty = all. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect")
	TArray<EFPSRLBoonChannel> AllowedChannels;

	/** Per-channel mechanic (a channel without an entry simply gets nothing from the Aspect itself). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect")
	TArray<FFPSRLAspectChannelGrants> ChannelGrants;

	/** Times the Upgrade Altar can improve this Aspect on one channel (0 = never offered). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aspect", meta = (ClampMin = "0"))
	int32 MaxUpgradeLevel = 1;

	bool AllowsChannel(EFPSRLBoonChannel Channel) const { return AllowedChannels.IsEmpty() || AllowedChannels.Contains(Channel); }

	/** The grants for a channel at an upgrade level (level > 0 uses UpgradedGrants when they're set). */
	const FFPSRLGrantSet* GetGrants(EFPSRLBoonChannel Channel, int32 UpgradeLevel) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
