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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLBoonStateChanged);

/**
 * A player's run build of Blessings (boons): three independent channels (Primary / Secondary / Ability), each with
 * one Aspect and its own count. Lives on AFPSRLPlayerState (one per player; never shared).
 *
 * Progression per channel: the first Blessing sets the channel's Aspect and is position 1. Later Blessings for the
 * channel come only from that Aspect. Position MinorPosition (3) is always the Aspect's Minor, MajorPosition (6) its
 * Major: at those positions the channel offers only its milestone, and every offer includes it. Up to
 * MaxBoonsPerChannel (11). An Aspect can be on only one channel: once Fire is on Primary it is never offered
 * for Secondary or Ability.
 *
 * Server-authoritative: the server rolls this player's options, validates the pick, applies it through GAS and
 * replicates the result. Clients only request (AFPSRLPlayerController::ServerSelectBoon / ServerRerollBoons).
 * One selection at a time, Blessing or Upgrade; every request carries the SelectionEventId, and the first valid
 * resolution wins, so double clicks and stale requests can't grant twice. Nobody waits for it and there is no timer.
 *
 *  Blessing altar: BeginSelection -> TrySelect / TryReroll (3 free, then Soul Fragments). Rerolls only replace the
 *    current options with a fresh set (avoiding the ones just shown when there are enough others); they never touch
 *    owned Blessings, Aspects or counts.
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

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLBoonOffer> CurrentOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	TArray<FFPSRLUpgradeOffer> UpgradeOptions;

	UPROPERTY(ReplicatedUsing = OnRep_BoonState, BlueprintReadOnly, Category = "Blessings")
	int32 SelectionEventId = 0;

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

	/** Blessing altar used: roll this player's options. False if a selection is already open or nothing is eligible. */
	bool BeginSelection();

	bool TrySelect(int32 EventId, int32 OptionIndex);

	/** Replaces the current options. Free while free rerolls remain, then costs Soul Fragments. */
	bool TryReroll(int32 EventId);

	/** Upgrade Altar used: offer upgradeable owned Blessings. False if a selection is open or nothing qualifies. */
	bool BeginUpgradeSelection();

	bool TrySelectUpgrade(int32 EventId, int32 OptionIndex);

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

	/** The Blessing pipeline: open channels -> Aspects -> valid pool -> milestones -> weighting -> unique picks. */
	/** Avoid: offers to leave out if enough other choices exist (a reroll passes the set it replaces). */
	TArray<FFPSRLBoonOffer> GenerateOptions(bool bForReroll, const TArray<FFPSRLBoonOffer>& Avoid = TArray<FFPSRLBoonOffer>()) const;
	TArray<FFPSRLUpgradeOffer> GenerateUpgradeOptions() const;

	void ApplyBoon(UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel);
	void GiveAspect(EFPSRLBoonChannel Channel);
	void GiveBoonStack(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack);
	/** One stack's share of one upgrade level. */
	void GiveBoonUpgrade(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack);
	void UpgradeBoon(EFPSRLBoonChannel Channel, int32 OwnedIndex);

	void EndSelection();
	void LogOptions(const TCHAR* What) const;
	void BroadcastChanged();
};
