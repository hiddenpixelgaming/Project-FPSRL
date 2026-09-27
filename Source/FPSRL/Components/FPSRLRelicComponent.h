// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/FPSRLGrantSet.h"
#include "FPSRLRelicComponent.generated.h"

class UAbilitySystemComponent;
class UFPSRLRelicDefinition;

/** One owned relic. */
USTRUCT(BlueprintType)
struct FFPSRLOwnedRelic
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Relics")
	TObjectPtr<UFPSRLRelicDefinition> Relic;

	UPROPERTY(BlueprintReadOnly, Category = "Relics")
	int32 Stacks = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLRelicStateChanged);

/**
 * A player's Relics for the current run: independent run modifiers, separate from Blessings (they never touch a
 * channel, an Aspect or a count). Lives on AFPSRLPlayerState. Server-authoritative; OwnedRelics replicates.
 *
 * Sources (chests, elites, bosses, merchants) don't exist yet: GrantRelic / GrantRandomRelic are the entry points,
 * reachable in development builds through the GrantRelic console command.
 * Rules: MaxStacks per relic; one relic per ExclusivityGroup; RequiredTags / BlockedTags against the player's tags.
 * GrantRandomRelic rolls a rarity by Project Settings > FPSRL Blessings > Relic Rarity Weights (rarities with no
 * eligible relic are skipped), then a relic of that rarity.
 * Travel: CopyRunStateTo / RestoreRunState like Blessings. Run end: ClearRunState removes exactly what was granted.
 */
UCLASS(ClassGroup = (FPSRL), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLRelicComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLRelicComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(ReplicatedUsing = OnRep_Relics, BlueprintReadOnly, Category = "Relics")
	TArray<FFPSRLOwnedRelic> OwnedRelics;

	UPROPERTY(BlueprintAssignable, Category = "Relics")
	FFPSRLRelicStateChanged OnRelicsChanged;

	UFUNCTION(BlueprintPure, Category = "Relics")
	int32 GetStacks(const UFPSRLRelicDefinition* Relic) const;

	UFUNCTION(BlueprintPure, Category = "Relics")
	bool CanReceive(const UFPSRLRelicDefinition* Relic) const;

	/** Server: adds one stack. False if not allowed (maxed, exclusive, requirements). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Relics")
	bool GrantRelic(UFPSRLRelicDefinition* Relic);

	/** Server: rolls a rarity, then a relic of it, from the relic pool. Returns what was granted (null if none). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Relics")
	UFPSRLRelicDefinition* GrantRandomRelic();

	void ClearRunState();
	void CopyRunStateTo(UFPSRLRelicComponent* Other) const;
	void RestoreRunState();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Relics();

private:
	UAbilitySystemComponent* GetAbilitySystem() const;
	void GiveStack(int32 OwnedIndex, bool bFirstStack);
	void BroadcastChanged();

	/** Server: per owned relic (same index), each stack's grants. */
	TArray<TArray<FFPSRLGrantHandles>> Handles;

	TArray<FFPSRLOwnedRelic> PendingRestore;
};
