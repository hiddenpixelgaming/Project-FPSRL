// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "FPSRLBoonComponent.generated.h"

class UAbilitySystemComponent;
class UFPSRLBoonDefinition;
class AFPSRLPlayerState;

/** One owned boon and how many times it has been taken. */
USTRUCT(BlueprintType)
struct FFPSRLOwnedBoon
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boons")
	TObjectPtr<UFPSRLBoonDefinition> Boon;

	UPROPERTY(BlueprintReadOnly, Category = "Boons")
	int32 Stacks = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLBoonStateChanged);

/**
 * A player's personal, run-scoped boon state. Lives on AFPSRLPlayerState (one per player).
 *
 * Server-authoritative: the server generates this player's choices, validates their pick or reroll, applies the boon
 * through GAS, and marks the selection complete. Clients only display the replicated state and send requests
 * (AFPSRLPlayerController::ServerSelectBoon / ServerRerollBoons). Another player's choices are never touched.
 *
 * Selection lifecycle: a Boon altar (AFPSRLBoonTerminal) calls BeginSelection -> TrySelect / TryReroll -> resolved.
 * Personal and optional: nobody waits for it, and no timer runs. Every request carries the SelectionEventId and the
 * first valid resolution wins, so a duplicate click or a stale request after reconnecting can never grant twice.
 * An unresolved choice is forfeited when the party leaves the Depth.
 *
 * Depth travel: owned boons are handed to the next Depth's PlayerState (CopyRunStateTo) and re-granted through GAS
 * there (RestoreRunState), so the build persists for the whole run.
 *
 * Rules (all numbers in UFPSRLBoonSettings): 3 options; 3 free rerolls then Soul Fragments; per-boon stacking;
 * at most 3 elements owned; +1% offer weight per owned boon of an element; capacity = ProgressionSet.MaxBoonSlots
 * (each stack uses a slot). No boon rarity. No in-run reset: boons are only ever added, until ClearRunState at run end.
 */
UCLASS(ClassGroup = (FPSRL), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLBoonComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLBoonComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// --- Replicated state ------------------------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Boons")
	TArray<FFPSRLOwnedBoon> OwnedBoons;

	/** This player's current choices (owner only). */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Boons")
	TArray<TObjectPtr<UFPSRLBoonDefinition>> CurrentOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Boons")
	int32 SelectionEventId = 0;

	/** A selection is open for this player and not yet resolved. */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Boons")
	bool bSelectionPending = false;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Boons")
	int32 FreeRerollsRemaining = 0;

	/** Fires on server and owning client whenever any of the above changes (bind UI here). */
	UPROPERTY(BlueprintAssignable, Category = "Boons")
	FFPSRLBoonStateChanged OnBoonStateChanged;

	// --- Queries ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Boons")
	int32 GetStacks(const UFPSRLBoonDefinition* Boon) const;

	/** Slots used: every stack of every boon counts. */
	UFUNCTION(BlueprintPure, Category = "Boons")
	int32 GetUsedBoonSlots() const;

	UFUNCTION(BlueprintPure, Category = "Boons")
	int32 GetMaxBoonSlots() const;

	/** Distinct elements this player owns boons of. */
	UFUNCTION(BlueprintPure, Category = "Boons")
	FGameplayTagContainer GetOwnedElements() const;

	/** Cost of the next reroll in Soul Fragments (0 while free rerolls remain). */
	UFUNCTION(BlueprintPure, Category = "Boons")
	int32 GetNextRerollCost() const;

	bool IsSelectionPending(int32 EventId) const { return bSelectionPending && EventId == SelectionEventId; }

	// --- Server API (called by AFPSRLBoonTerminal / AFPSRLPlayerController) -----------------------------------

	/**
	 * Opens a new choice for this player (a Boon altar was used). False if one is already open, or if nothing can be
	 * offered (e.g. slots are full). Each call gets a new SelectionEventId, so stale requests for an older one fail.
	 */
	bool BeginSelection();

	/** Player's manual pick. Returns true if it resolved the selection. */
	bool TrySelect(int32 EventId, int32 OptionIndex);

	/** Replaces the current options. Free while free rerolls remain, then costs Soul Fragments. */
	bool TryReroll(int32 EventId);

	/** Adds a boon outside a selection (rewards, dev cheat). Respects stacking and capacity. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boons")
	bool GrantBoon(UFPSRLBoonDefinition* Boon);

	/** Run end: removes exactly the effects this component granted and clears all temporary boon state. */
	void ClearRunState();

	/** Server, seamless travel: hands this player's owned boons to the next Depth's component. */
	void CopyRunStateTo(UFPSRLBoonComponent* Other) const;

	/** Server: re-grants boons carried over from the previous Depth (called from BeginRunState). */
	void RestoreRunState();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_BoonState();

private:
	/** Server: per OwnedBoons entry (same index), the grants of each stack. */
	TArray<TArray<FFPSRLGrantHandles>> OwnedBoonHandles;

	/** Server: boons carried from the previous Depth, waiting for BeginRunState to re-grant them. */
	TArray<FFPSRLOwnedBoon> PendingRestore;

	AFPSRLPlayerState* GetOwningPlayerState() const;
	UAbilitySystemComponent* GetAbilitySystem() const;

	/** Tags describing this player's build: weapon, aspect/build tags on the ASC, owned elements. */
	FGameplayTagContainer GetBuildTags() const;

	bool IsEligible(const UFPSRLBoonDefinition* Boon, bool bForReroll) const;
	float GetOfferWeight(const UFPSRLBoonDefinition* Boon) const;
	TArray<TObjectPtr<UFPSRLBoonDefinition>> GenerateOptions(bool bForReroll) const;

	bool AddBoonStack(UFPSRLBoonDefinition* Boon);
	void ResolveSelection(UFPSRLBoonDefinition* Chosen, const TCHAR* How);
	void BroadcastChanged();
};
