// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Data/FPSRLGrantSet.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLBoonComponent.generated.h"

class UAbilitySystemComponent;
class UFPSRLAspectDefinition;
class UFPSRLBoonDefinition;
class AFPSRLPlayerState;

/** One owned Blessing on a channel. */
USTRUCT(BlueprintType)
struct FFPSRLOwnedBoon
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	TObjectPtr<UFPSRLBoonDefinition> Boon;

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	int32 Stacks = 0;

	/** 0 = base; each Upgrade Altar pick adds 1 (upgrades stack up to the Blessing's MaxUpgradeLevel; UI shows gold). */
	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	int32 UpgradeLevel = 0;
};

/** One channel's progression: its Aspect, how many Blessings it has, and which. */
USTRUCT(BlueprintType)
struct FFPSRLBoonTrack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;

	/** Set by the channel's first Blessing; null until then. */
	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	TObjectPtr<UFPSRLAspectDefinition> Aspect;

	/** Blessings taken on this channel (every stack counts). Position of the next one = Count + 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	int32 Count = 0;

	/** In the order they were first taken. */
	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLOwnedBoon> Boons;
};

/** One Blessing altar choice. */
USTRUCT(BlueprintType)
struct FFPSRLBoonOffer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	TObjectPtr<UFPSRLBoonDefinition> Boon;

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;

	/** Taking it establishes the channel's Aspect (the channel is empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	bool bNewAspect = false;
};

/** One Upgrade Altar choice: an owned Blessing on a channel (Aspects are never upgraded). */
USTRUCT(BlueprintType)
struct FFPSRLUpgradeOffer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;

	UPROPERTY(BlueprintReadOnly, Category = "Blessings")
	TObjectPtr<UFPSRLBoonDefinition> Boon;
};

/** What the player is currently choosing, if anything (one at a time). */
UENUM(BlueprintType)
enum class EFPSRLBoonSelectionKind : uint8
{
	None,
	Blessing,	// Blessing altar
	Upgrade		// Upgrade Altar
};

/** Where a Blessing altar choice is: Aspect first, then a slot (only for a new Aspect), then the Blessing. */
UENUM(BlueprintType)
enum class EFPSRLAltarStep : uint8
{
	ChooseAspect,
	ChooseSlot,
	ChooseBlessing
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLBoonStateChanged);

/**
 * A player's run build of Blessings (boons): three independent channels (Primary / Secondary / Ability), each with
 * one Aspect and its own count. Lives on AFPSRLPlayerState (one per player; never shared).
 *
 * Progression per channel: the first Blessing sets the channel's Aspect; later Blessings for the channel come only from
 * that Aspect, up to MaxBoonsPerChannel (11). An Aspect can be on only one channel (once Fire is on Primary it is never
 * offered for Secondary or Ability), and slots may stay empty. Every Blessing is a Minor or a Major; each Blessing choice
 * is a Minor or a Major by weight (MinorWeight : MajorWeight, Minors commoner), Majors only once the slot has
 * MajorMinBlessings.
 *
 * Server-authoritative: the server rolls this player's options, validates the pick, applies it through GAS and
 * replicates the result. Clients only request (AFPSRLPlayerController::ServerSelectBoon / ServerRerollBoons).
 * One selection at a time, Blessing or Upgrade; every request carries the SelectionEventId, and the first valid
 * resolution wins, so double clicks and stale requests can't grant twice. Nobody waits for it and there is no timer.
 *
 *  Blessing altar, three steps: BeginSelection offers AspectOptionsPerAltar Aspects (the player's own weighted
 *    AssignedAspectWeight times higher; unassigned ones only while a slot is free) -> TryChooseAspect -> TryChooseSlot
 *    (only for a new Aspect with more than one free slot) -> TrySelect from BoonOptionsPerSelection (2) Blessings.
 *    TryBack steps back; the Blessings rolled for an Aspect and slot are kept for the altar, so going back never
 *    re-rolls them. TryReroll (Aspect step only; 3 free per RUN shared by every altar, refilled when the run ends, then
 *    Soul Fragments) replaces the Aspect choices; it never touches owned Blessings, Aspects or counts.
 *  Upgrade Altar: BeginUpgradeSelection -> TrySelectUpgrade. Offers up to UpgradeOptionsPerSelection of the
 *    player's owned Blessings below their MaxUpgradeLevel (any channel; Aspects are never upgraded). Upgrades stack
 *    (a Blessing can be upgraded again at a later altar) and never change a count.
 *
 * Depth travel: the tracks are handed to the next Depth's PlayerState (CopyRunStateTo) and re-granted there
 * (RestoreRunState). Run end: ClearRunState removes exactly what was granted and empties everything.
 */
UCLASS(ClassGroup = (FPSRL), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLBoonComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLBoonComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// --- Replicated state ------------------------------------------------------------------------------------

	/** One entry per channel (index = channel). Everyone may see a teammate's build. */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLBoonTrack> Tracks;

	/** What the owner is choosing right now (owner only, like everything below). */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	EFPSRLBoonSelectionKind PendingKind = EFPSRLBoonSelectionKind::None;

	/** Blessing altar: the current step and its choices (Aspects, then slots, then Blessings). */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	EFPSRLAltarStep AltarStep = EFPSRLAltarStep::ChooseAspect;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<TObjectPtr<UFPSRLAspectDefinition>> AspectOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<EFPSRLBoonChannel> SlotOptions;

	/** The Aspect picked at step 1 (and its slot, once known). */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TObjectPtr<UFPSRLAspectDefinition> ChosenAspect;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	EFPSRLBoonChannel ChosenChannel = EFPSRLBoonChannel::Primary;

	/** Step 3: the Blessing choices for ChosenAspect on ChosenChannel. */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLBoonOffer> CurrentOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLUpgradeOffer> UpgradeOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	int32 SelectionEventId = 0;

	/** Nothing left to pick at a Blessing altar (every open slot full or out of Blessings, no free slot a new Aspect could
	 *  take). From then on Blessing altars act as Upgrade Altars for this player for the rest of the run. */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	bool bAllBlessingsChosen = false;

	/** Free rerolls left THIS RUN (all altars share them); refilled only when the run ends (ClearRunState). */
	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	int32 FreeRerollsRemaining = 0;

	/** Fires on server and owning client whenever any of the above changes (bind UI here). */
	UPROPERTY(BlueprintAssignable, Category = "Blessings")
	FFPSRLBoonStateChanged OnBoonStateChanged;

	// --- Queries ---------------------------------------------------------------------------------------------

	const FFPSRLBoonTrack& GetTrack(EFPSRLBoonChannel Channel) const { return Tracks[static_cast<int32>(Channel)]; }

	UFUNCTION(BlueprintPure, Category = "Blessings")
	int32 GetStacks(EFPSRLBoonChannel Channel, const UFPSRLBoonDefinition* Boon) const;

	/** The item the channel holds (Weapon.* / Secondary.* / Ability.*); empty = channel has nothing to bless. */
	FGameplayTag GetChannelItem(EFPSRLBoonChannel Channel) const;

	/** Cost of the next reroll in Soul Fragments (0 while free rerolls remain). */
	UFUNCTION(BlueprintPure, Category = "Blessings")
	int32 GetNextRerollCost() const;

	bool IsSelectionPending(EFPSRLBoonSelectionKind Kind, int32 EventId) const { return PendingKind == Kind && EventId == SelectionEventId; }
	bool HasPendingSelection() const { return PendingKind != EFPSRLBoonSelectionKind::None; }

	/** "Primary" / "Secondary" / "Ability", for UI. */
	static FText GetChannelName(EFPSRLBoonChannel Channel);

	// --- Server API (altars / AFPSRLPlayerController) -------------------------------------------------------------

	/** A Blessing altar was used: its three-step Blessing choice, or an upgrade choice once bAllBlessingsChosen. False
	 *  if a selection is already open or there is nothing to offer. */
	bool BeginAltar();

	/** Blessing altar used: offer this player's Aspect choices. False if a selection is already open or nothing fits. */
	bool BeginSelection();

	/** Step 1: pick an Aspect. An owned one goes straight to its Blessings; a new one to the slot step (or straight to
	 *  Blessings when only one slot is free). */
	bool TryChooseAspect(int32 EventId, int32 OptionIndex);

	/** Step 2: pick the free slot for a new Aspect. */
	bool TryChooseSlot(int32 EventId, int32 OptionIndex);

	/** Step 3: pick the Blessing. */
	bool TrySelect(int32 EventId, int32 OptionIndex);

	/** Back one step (Blessing -> slot or Aspect, slot -> Aspect). */
	bool TryBack(int32 EventId);

	/** Step 1 only: replaces the Aspect choices. Free while this run's free rerolls remain, then costs Soul Fragments. */
	bool TryReroll(int32 EventId);

	/** Upgrade Altar used: offer upgradeable owned Blessings. False if a selection is open or nothing qualifies. */
	bool BeginUpgradeSelection();

	bool TrySelectUpgrade(int32 EventId, int32 OptionIndex);

	/** Test only: close any open choice without picking (headless checks). */
	void TestCancelSelection() { EndSelection(); }

	/** Grants a Blessing outside an altar (rewards, test command), with the same rules as an altar pick. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Blessings")
	bool GrantBoon(UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel);

	/** Run end: removes exactly what this component granted and clears all Blessing state. */
	void ClearRunState();

	/** Server, seamless travel: hands the build to the next Depth's component. */
	void CopyRunStateTo(UFPSRLBoonComponent* Other) const;

	/** Server: re-grants the build carried over from the previous Depth (called from BeginRunState). */
	void RestoreRunState();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_BoonState();

private:
	/** Server-only record of what was granted per channel, parallel to Tracks[Channel].Boons. */
	struct FChannelHandles
	{
		FFPSRLGrantHandles Aspect;
		TArray<TArray<FFPSRLGrantHandles>> Boons;	// [owned boon][stack]
	};
	FChannelHandles Handles[static_cast<int32>(EFPSRLBoonChannel::MAX)];

	/** Server: build carried from the previous Depth, waiting for BeginRunState. */
	TArray<FFPSRLBoonTrack> PendingRestore;

	AFPSRLPlayerState* GetOwningPlayerState() const;
	UAbilitySystemComponent* GetAbilitySystem() const;
	FFPSRLBoonTrack& GetMutableTrack(EFPSRLBoonChannel Channel) { return Tracks[static_cast<int32>(Channel)]; }

	/** Can take Blessings at all: holds an item and isn't full. */
	bool IsChannelOpen(EFPSRLBoonChannel Channel) const;

	/** Can this Blessing be taken on this channel right now (as the channel's next position)? */
	bool IsEligible(const UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel, bool bForReroll) const;

	/** The channel holding this Aspect, or MAX if none. */
	EFPSRLBoonChannel FindAspectChannel(const UFPSRLAspectDefinition* Aspect) const;

	/** Free, open slots a new Aspect could take (each with at least one eligible first Blessing). */
	TArray<EFPSRLBoonChannel> GetFreeSlotsFor(const UFPSRLAspectDefinition* Aspect) const;

	/** Step 1 choices: the player's own Aspects (weighted higher) and, while a slot is free, new ones. Avoid: leave out
	 *  if enough others exist (a reroll passes the set it replaces). */
	TArray<UFPSRLAspectDefinition*> GenerateAspectOptions(const TArray<TObjectPtr<UFPSRLAspectDefinition>>& Avoid) const;

	/** Step 3 choices for an Aspect on a channel, each with a chance of being its Minor or Major. */
	TArray<FFPSRLBoonOffer> GenerateBlessingOptions(UFPSRLAspectDefinition* Aspect, EFPSRLBoonChannel Channel) const;

	/** Moves to step 3 for ChosenAspect / ChosenChannel (rolls its Blessings once per altar, then reuses them). */
	bool EnterBlessingStep();

	/** Server: Blessings already rolled at this altar, by Aspect and channel, so Back can't be used to re-roll them. */
	TMap<FString, TArray<FFPSRLBoonOffer>> RolledBlessings;
	TArray<FFPSRLUpgradeOffer> GenerateUpgradeOptions() const;

	void ApplyBoon(UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel);
	void GiveAspect(EFPSRLBoonChannel Channel);
	void GiveBoonStack(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack);
	/** One stack's share of one upgrade level. */
	void GiveBoonUpgrade(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack);
	void UpgradeBoon(EFPSRLBoonChannel Channel, int32 OwnedIndex);

	void EndSelection();
	/** Server: recompute bAllBlessingsChosen (after any change to the build). */
	void RefreshAllBlessingsChosen();
	void LogOptions(const TCHAR* What) const;
	void BroadcastChanged();
};
