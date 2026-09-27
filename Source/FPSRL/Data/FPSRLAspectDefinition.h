// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLAspectDefinition.generated.h"

class UTexture2D;

/** What having this Aspect on one channel gives the player. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLAspectChannelGrants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aspect")
	EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;

	/** The Aspect's own mechanic on this channel, granted with the channel's first Blessing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aspect")
	FFPSRLGrantSet Grants;
};

/**
 * An Aspect: a Blessing family (Air, Fire, Water, Earth, Light, Dark). Pure data, one asset per Aspect
 * (Primary Asset "Aspect"); Blessings name their Aspect (UFPSRLBoonDefinition::Aspect).
 *
 * A player's channels (Primary / Secondary / Ability) each hold at most one Aspect, set by the channel's first
 * Blessing, with its own count and milestones (see UFPSRLBoonComponent). An Aspect can be on only one
 * of a player's channels at a time.
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

	bool AllowsChannel(EFPSRLBoonChannel Channel) const { return AllowedChannels.IsEmpty() || AllowedChannels.Contains(Channel); }

	/** The grants for a channel (null if the Aspect gives that channel nothing of its own). Aspects are never upgraded;
	 *  the Upgrade Altar only improves Blessings. */
	const FFPSRLGrantSet* GetGrants(EFPSRLBoonChannel Channel) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
