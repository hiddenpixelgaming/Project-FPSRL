// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "FPSRLAspectComponent.generated.h"

class UFPSRLAspectDefinition;
class AFPSRLPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLAspectStateChanged);

/**
 * A player's Aspect: ONE per run, chosen in the Lobby right after the weapon, locked for the whole run.
 * Lives on AFPSRLPlayerState. Server-authoritative; clients display options and request a pick
 * (AFPSRLPlayerController::ServerSelectAspect).
 *
 *  Lobby: OfferAspects(weapon) generates up to AspectOptionsPerWeapon compatible choices (new event id). Changing
 *         weapon in the Lobby re-offers, because the old aspect would no longer fit.
 *  Run:   ApplyActiveAspect() on arrival grants AspectTag + Grants through GAS (spec tagged Effect.Temporary.Run).
 *  End:   ClearRunState() removes exactly those grants and forgets the aspect.
 * ActiveAspect is carried from the Lobby's PlayerState into the run by AFPSRLPlayerState::CopyProperties.
 * There is no aspect reset, removal or replacement during a run, and boon rerolls never touch it.
 */
UCLASS(ClassGroup = (FPSRL), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLAspectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLAspectComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(ReplicatedUsing = OnRep_AspectState, BlueprintReadOnly, Category = "Aspect")
	TObjectPtr<UFPSRLAspectDefinition> ActiveAspect;

	/** Current choices (owner only); empty once one is chosen. */
	UPROPERTY(ReplicatedUsing = OnRep_AspectState, BlueprintReadOnly, Category = "Aspect")
	TArray<TObjectPtr<UFPSRLAspectDefinition>> AspectOptions;

	UPROPERTY(ReplicatedUsing = OnRep_AspectState, BlueprintReadOnly, Category = "Aspect")
	int32 AspectEventId = 0;

	UPROPERTY(BlueprintAssignable, Category = "Aspect")
	FFPSRLAspectStateChanged OnAspectStateChanged;

	/** True once an aspect is chosen, or if the pool has none for this weapon (nothing to choose). */
	UFUNCTION(BlueprintPure, Category = "Aspect")
	bool HasCompletedAspectChoice() const;

	// --- Server API ---
	void OfferAspects(const FGameplayTag& WeaponTag);
	bool TrySelectAspect(int32 EventId, int32 OptionIndex, const FGameplayTag& WeaponTag);
	void ApplyActiveAspect();
	void ClearRunState();

	/** Copy the chosen aspect to the next PlayerState across seamless travel (data only, no grants). */
	void CopyChoiceTo(UFPSRLAspectComponent* Other) const;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_AspectState();

private:
	AFPSRLPlayerState* GetOwningPlayerState() const;
	void BroadcastChanged();

	/** Server: whether any aspect exists for the weapon last offered (so a weapon without aspects never blocks Start). */
	bool bAspectsAvailableForWeapon = false;

	FFPSRLGrantHandles AppliedHandles;
	bool bApplied = false;
};
