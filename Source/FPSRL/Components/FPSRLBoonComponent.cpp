// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLBoonComponent.h"
#include "Combat/FPSRLCombatRules.h"
#include "Combat/FPSRLWeapon.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "TimerManager.h"
#include "AbilitySystemGlobals.h"
#include "Data/FPSRLBlessingEffects.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

#define LOCTEXT_NAMESPACE "FPSRLBlessings"

namespace FPSRLBoons
{
	constexpr int32 NumChannels = static_cast<int32>(EFPSRLBoonChannel::MAX);

	/** Weighted pick; returns INDEX_NONE for an empty list. */
	template <typename T, typename WeightFn>
	int32 PickWeighted(const TArray<T>& Items, WeightFn GetWeight)
	{
		float Total = 0.f;
		for (const T& Item : Items)
		{
			Total += FMath::Max(0.f, GetWeight(Item));
		}
		if (Items.IsEmpty())
		{
			return INDEX_NONE;
		}
		if (Total <= 0.f)
		{
			return FMath::RandRange(0, Items.Num() - 1);
		}
		float Roll = FMath::FRandRange(0.f, Total);
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			Roll -= FMath::Max(0.f, GetWeight(Items[Index]));
			if (Roll <= 0.f)
			{
				return Index;
			}
		}
		return Items.Num() - 1;
	}

	template <typename T>
	void Shuffle(TArray<T>& Items)
	{
		for (int32 Index = Items.Num() - 1; Index > 0; --Index)
		{
			Items.Swap(Index, FMath::RandRange(0, Index));
		}
	}
}

UFPSRLBoonComponent::UFPSRLBoonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	FreeRerollsRemaining = UFPSRLBoonSettings::Get().FreeRerollsPerRun;

	Tracks.SetNum(FPSRLBoons::NumChannels);
	for (int32 Index = 0; Index < FPSRLBoons::NumChannels; ++Index)
	{
		Tracks[Index].Channel = static_cast<EFPSRLBoonChannel>(Index);
	}
}

void UFPSRLBoonComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFPSRLBoonComponent, Tracks);	// teammates may inspect each other's build
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, PendingKind, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, AltarStep, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, AspectOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, SlotOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, ChosenAspect, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, ChosenChannel, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, CurrentOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, UpgradeOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, SelectionEventId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, FreeRerollsRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, bAllBlessingsChosen, COND_OwnerOnly);
}

void UFPSRLBoonComponent::OnRep_BoonState()
{
	OnBoonStateChanged.Broadcast();
}

void UFPSRLBoonComponent::BroadcastChanged()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnBoonStateChanged.Broadcast();	// server-side listeners (and the listen host's own UI)
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RefreshPeriodicProcs();	// a Periodic Blessing gained or lost
	}
}

AFPSRLPlayerState* UFPSRLBoonComponent::GetOwningPlayerState() const
{
	return Cast<AFPSRLPlayerState>(GetOwner());
}

UAbilitySystemComponent* UFPSRLBoonComponent::GetAbilitySystem() const
{
	const AFPSRLPlayerState* PS = GetOwningPlayerState();
	return PS ? PS->GetAbilitySystemComponent() : nullptr;
}

// --- Queries -----------------------------------------------------------------------------------------------------

FText UFPSRLBoonComponent::GetChannelName(EFPSRLBoonChannel Channel)
{
	switch (Channel)
	{
	case EFPSRLBoonChannel::Primary:	return LOCTEXT("Primary", "Primary");
	case EFPSRLBoonChannel::Secondary:	return LOCTEXT("Secondary", "Secondary");
	case EFPSRLBoonChannel::Ability:	return LOCTEXT("Ability", "Ability");
	default:							return FText::GetEmpty();
	}
}

int32 UFPSRLBoonComponent::GetStacks(EFPSRLBoonChannel Channel, const UFPSRLBoonDefinition* Boon) const
{
	const FFPSRLOwnedBoon* Owned = GetTrack(Channel).Boons.FindByPredicate([Boon](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Boon; });
	return Owned ? Owned->Stacks : 0;
}

FGameplayTag UFPSRLBoonComponent::GetChannelItem(EFPSRLBoonChannel Channel) const
{
	const AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS)
	{
		return FGameplayTag();
	}
	switch (Channel)
	{
	case EFPSRLBoonChannel::Primary:	return PS->SelectedWeapon;
	case EFPSRLBoonChannel::Secondary:	return PS->SecondaryItem;
	case EFPSRLBoonChannel::Ability:	return PS->AbilityItem;
	default:							return FGameplayTag();
	}
}

int32 UFPSRLBoonComponent::GetNextRerollCost() const
{
	return FreeRerollsRemaining > 0 ? 0 : UFPSRLBoonSettings::Get().RerollCost;
}

bool UFPSRLBoonComponent::IsChannelOpen(EFPSRLBoonChannel Channel) const
{
	return GetChannelItem(Channel).IsValid() && GetTrack(Channel).Count < UFPSRLBoonSettings::Get().MaxBoonsPerChannel;
}

bool UFPSRLBoonComponent::IsEligible(const UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel, bool bForReroll) const
{
	if (!Boon || !Boon->Aspect || Boon->SelectionWeight <= 0.f || (bForReroll && !Boon->bCanReroll) || !Boon->AllowsChannel(Channel))
	{
		return false;
	}

	// The channel's Aspect decides the family. An empty channel takes any Aspect the player doesn't already have on
	// another channel: one Aspect per channel and each Aspect on one channel only (Fire on Primary rules out Fire on
	// Secondary and Ability).
	const FFPSRLBoonTrack& Track = GetTrack(Channel);
	if ((Track.Aspect && Boon->Aspect != Track.Aspect) || Track.Count < Boon->RequiredChannelCount)
	{
		return false;
	}
	if (!Track.Aspect)
	{
		for (const FFPSRLBoonTrack& Other : Tracks)
		{
			if (Other.Channel != Channel && Other.Aspect == Boon->Aspect)
			{
				return false;
			}
		}
	}
	if (GetStacks(Channel, Boon) >= Boon->GetMaxStacks())
	{
		return false;
	}

	// It must work with the kind of attack the channel's item makes (Ranged / Melee / Ability; none listed = Universal).
	if (!Boon->SupportsSource(GetChannelSource(Channel)))
	{
		return false;
	}

	// The item in the channel must support it (e.g. a melee-only Blessing needs Secondary.Melee).
	const FGameplayTag Item = GetChannelItem(Channel);
	if (!Boon->RequiredItemTags.IsEmpty() && !(Item.IsValid() && Item.MatchesAny(Boon->RequiredItemTags)))
	{
		return false;
	}

	for (const UFPSRLBoonDefinition* Prerequisite : Boon->RequiredBoons)
	{
		if (Prerequisite && GetStacks(Channel, Prerequisite) == 0)
		{
			return false;
		}
	}

	if (!Boon->BlockedTags.IsEmpty())
	{
		FGameplayTagContainer Carried;
		if (const UAbilitySystemComponent* ASC = GetAbilitySystem())
		{
			Carried.AppendTags(ASC->GetOwnedGameplayTags());
		}
		for (const FFPSRLBoonTrack& AnyTrack : Tracks)
		{
			for (const FFPSRLOwnedBoon& Owned : AnyTrack.Boons)
			{
				if (Owned.Boon)
				{
					Carried.AppendTags(Owned.Boon->BoonTags);
				}
			}
		}
		if (Carried.HasAny(Boon->BlockedTags))
		{
			return false;
		}
	}
	return true;
}

// --- Offer generation --------------------------------------------------------------------------------------------

EFPSRLBoonChannel UFPSRLBoonComponent::FindAspectChannel(const UFPSRLAspectDefinition* Aspect) const
{
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		if (Aspect && Track.Aspect == Aspect)
		{
			return Track.Channel;
		}
	}
	return EFPSRLBoonChannel::MAX;
}

TArray<EFPSRLBoonChannel> UFPSRLBoonComponent::GetFreeSlotsFor(const UFPSRLAspectDefinition* Aspect) const
{
	TArray<EFPSRLBoonChannel> Slots;
	const UFPSRLBoonPool* Pool = UFPSRLBoonSettings::Get().BoonPool.LoadSynchronous();
	if (!Pool || !Aspect)
	{
		return Slots;
	}
	for (const EFPSRLBoonChannel Channel : TEnumRange<EFPSRLBoonChannel>())
	{
		if (!IsChannelOpen(Channel) || GetTrack(Channel).Aspect || !Aspect->AllowsChannel(Channel))
		{
			continue;	// no item (e.g. no Ability yet), full, or already holding an Aspect
		}
		const bool bHasFirstBlessing = Pool->Boons.ContainsByPredicate([this, Aspect, Channel](const UFPSRLBoonDefinition* Boon)
		{
			return Boon && Boon->Aspect == Aspect && IsEligible(Boon, Channel, false);
		});
		if (bHasFirstBlessing)
		{
			Slots.Add(Channel);
		}
	}
	return Slots;
}

TArray<UFPSRLAspectDefinition*> UFPSRLBoonComponent::GenerateAspectOptions(const TArray<TObjectPtr<UFPSRLAspectDefinition>>& Avoid) const
{
	struct FCandidate
	{
		UFPSRLAspectDefinition* Aspect = nullptr;
		float Weight = 1.f;
	};

	TArray<UFPSRLAspectDefinition*> Options;
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const UFPSRLBoonPool* Pool = Settings.BoonPool.LoadSynchronous();
	if (!Pool)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Blessings] No Blessing pool set in Project Settings > FPSRL Blessings"));
		return Options;
	}

	// Every Aspect the pool has Blessings for. The player's own ones (with room left on their slot) are weighted
	// AssignedAspectWeight times higher; new ones only appear while a free slot could take them.
	TArray<UFPSRLAspectDefinition*> Aspects;
	for (const UFPSRLBoonDefinition* Boon : Pool->Boons)
	{
		if (Boon && Boon->Aspect)
		{
			Aspects.AddUnique(Boon->Aspect);
		}
	}
	TArray<FCandidate> Candidates;
	for (UFPSRLAspectDefinition* Aspect : Aspects)
	{
		const EFPSRLBoonChannel Channel = FindAspectChannel(Aspect);
		if (Channel != EFPSRLBoonChannel::MAX)
		{
			// Only if its next position has something to offer (same rules as the Blessing step).
			const bool bHasNext = IsChannelOpen(Channel) && !GenerateBlessingOptions(Aspect, Channel).IsEmpty();
			if (bHasNext)
			{
				Candidates.Add({ Aspect, Settings.AssignedAspectWeight });
			}
		}
		else if (!GetFreeSlotsFor(Aspect).IsEmpty())
		{
			Candidates.Add({ Aspect, 1.f });
		}
	}

	// A reroll should feel like a new set: the new Aspects just shown are less likely (not left out, which with few
	// Aspects made a reroll always "the other ones"). The player's own Aspects keep their full weight.
	const int32 Count = Settings.AspectOptionsPerAltar;
	for (FCandidate& Candidate : Candidates)
	{
		if (Avoid.Contains(Candidate.Aspect) && FindAspectChannel(Candidate.Aspect) == EFPSRLBoonChannel::MAX)
		{
			Candidate.Weight *= Settings.RerollRepeatWeight;
		}
	}

	// Sets this roll must not repeat exactly: the one being rerolled, and every teammate's open Aspect choices (each
	// player rolls their own; two players at the same altar should never see the identical set).
	auto SameSet = [](const TArray<UFPSRLAspectDefinition*>& A, const TArray<TObjectPtr<UFPSRLAspectDefinition>>& B)
	{
		return A.Num() == B.Num() && !A.ContainsByPredicate([&B](UFPSRLAspectDefinition* Aspect) { return !B.Contains(Aspect); });
	};
	TArray<const TArray<TObjectPtr<UFPSRLAspectDefinition>>*> Forbidden;
	if (!Avoid.IsEmpty())
	{
		Forbidden.Add(&Avoid);
	}
	const AFPSRLPlayerState* Self = GetOwningPlayerState();
	const AGameStateBase* GameState = Self && Self->GetWorld() ? Self->GetWorld()->GetGameState() : nullptr;
	if (GameState)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			const AFPSRLPlayerState* Teammate = Cast<AFPSRLPlayerState>(Other);
			const UFPSRLBoonComponent* TeammateBoons = Teammate && Teammate != Self ? Teammate->GetBoonComponent() : nullptr;
			if (TeammateBoons && TeammateBoons->PendingKind == EFPSRLBoonSelectionKind::Blessing && !TeammateBoons->AspectOptions.IsEmpty())
			{
				Forbidden.Add(&TeammateBoons->AspectOptions);
			}
		}
	}

	// Roll; if the set matches a forbidden one, roll again (a few tries, then accept: with only 3 Aspects possible
	// there may be no other set).
	for (int32 Attempt = 0; Attempt < 20; ++Attempt)
	{
		TArray<FCandidate> Remaining = Candidates;
		Options.Reset();
		while (Options.Num() < Count && !Remaining.IsEmpty())
		{
			const int32 Pick = FPSRLBoons::PickWeighted(Remaining, [](const FCandidate& Candidate) { return Candidate.Weight; });
			Options.Add(Remaining[Pick].Aspect);
			Remaining.RemoveAtSwap(Pick);
		}
		if (!Forbidden.ContainsByPredicate([&](const TArray<TObjectPtr<UFPSRLAspectDefinition>>* Set) { return SameSet(Options, *Set); }))
		{
			break;
		}
	}
	FPSRLBoons::Shuffle(Options);
	return Options;
}

TArray<FFPSRLBoonOffer> UFPSRLBoonComponent::GenerateBlessingOptions(UFPSRLAspectDefinition* Aspect, EFPSRLBoonChannel Channel) const
{
	TArray<FFPSRLBoonOffer> Options;
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const UFPSRLBoonPool* Pool = Settings.BoonPool.LoadSynchronous();
	if (!Pool || !Aspect)
	{
		return Options;
	}

	// The next position on this slot decides the kind (BoonSettings.PositionTypes: 3rd = Minor, 6th = Major by default,
	// the rest Normal). A Minor or Major position with none left falls back to Normal (a missing Minor never blocks
	// progress); a Normal position never offers a Minor or Major. Nothing left = no options (the Aspect isn't offered).
	const FFPSRLBoonTrack& Track = GetTrack(Channel);
	const EFPSRLBoonType Wanted = Settings.GetTypeForPosition(Track.Count + 1);
	TArray<UFPSRLBoonDefinition*> OfKind, Normal;
	for (UFPSRLBoonDefinition* Boon : Pool->Boons)
	{
		if (!Boon || Boon->Aspect != Aspect || !IsEligible(Boon, Channel, false))
		{
			continue;
		}
		if (Boon->BoonType == Wanted)
		{
			OfKind.Add(Boon);
		}
		if (Boon->BoonType == EFPSRLBoonType::Normal)
		{
			Normal.Add(Boon);
		}
	}
	TArray<UFPSRLBoonDefinition*>& From = !OfKind.IsEmpty() || Wanted == EFPSRLBoonType::Normal ? OfKind : Normal;
	while (Options.Num() < Settings.BoonOptionsPerSelection && !From.IsEmpty())
	{
		const int32 Pick = FPSRLBoons::PickWeighted(From, [](const UFPSRLBoonDefinition* Boon) { return Boon->SelectionWeight; });
		Options.Add({ From[Pick], Channel, Track.Aspect == nullptr });
		From.RemoveAtSwap(Pick);
	}
	return Options;
}


TArray<FFPSRLUpgradeOffer> UFPSRLBoonComponent::GenerateUpgradeOptions() const
{
	TArray<FFPSRLUpgradeOffer> Pool;
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			if (Owned.Boon && Owned.UpgradeLevel < Owned.Boon->MaxUpgradeLevel)
			{
				Pool.Add({ Track.Channel, Owned.Boon });
			}
		}
	}
	FPSRLBoons::Shuffle(Pool);
	Pool.SetNum(FMath::Min(Pool.Num(), UFPSRLBoonSettings::Get().UpgradeOptionsPerSelection));
	return Pool;
}

// --- Blessing altar ----------------------------------------------------------------------------------------------

bool UFPSRLBoonComponent::BeginAltar()
{
	// Once there is nothing left to pick, a Blessing altar becomes an Upgrade Altar for this player.
	return bAllBlessingsChosen ? BeginUpgradeSelection() : BeginSelection();
}

void UFPSRLBoonComponent::RefreshAllBlessingsChosen()
{
	if (GetOwner()->HasAuthority())
	{
		bAllBlessingsChosen = GenerateAspectOptions({}).IsEmpty();
	}
}

bool UFPSRLBoonComponent::BeginSelection()
{
	if (!GetOwner()->HasAuthority() || HasPendingSelection())
	{
		return false;	// one open choice at a time; the pending one stays as it is
	}
	TArray<UFPSRLAspectDefinition*> Options = GenerateAspectOptions({});
	if (Options.IsEmpty())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: nothing to offer"), *GetNameSafe(GetOwner()));
		return false;
	}
	EndSelection();
	++SelectionEventId;
	PendingKind = EFPSRLBoonSelectionKind::Blessing;
	AltarStep = EFPSRLAltarStep::ChooseAspect;
	AspectOptions = Options;
	LogOptions(TEXT("altar opened"));
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TryChooseAspect(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId)
		|| AltarStep != EFPSRLAltarStep::ChooseAspect || !AspectOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	UFPSRLAspectDefinition* Aspect = AspectOptions[OptionIndex];
	const EFPSRLBoonChannel OwnedChannel = FindAspectChannel(Aspect);
	ChosenAspect = Aspect;
	if (OwnedChannel != EFPSRLBoonChannel::MAX)
	{
		// Already on a slot: straight to its Blessings.
		SlotOptions.Reset();
		ChosenChannel = OwnedChannel;
		return EnterBlessingStep();
	}

	// A new Aspect: pick one of the free slots it fits (skipped when only one does).
	SlotOptions = GetFreeSlotsFor(Aspect);
	if (SlotOptions.IsEmpty())
	{
		return false;
	}
	if (SlotOptions.Num() == 1)
	{
		ChosenChannel = SlotOptions[0];
		return EnterBlessingStep();
	}
	AltarStep = EFPSRLAltarStep::ChooseSlot;
	LogOptions(TEXT("Aspect chosen, choose a slot"));
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TryChooseSlot(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId)
		|| AltarStep != EFPSRLAltarStep::ChooseSlot || !SlotOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	ChosenChannel = SlotOptions[OptionIndex];
	return EnterBlessingStep();
}

bool UFPSRLBoonComponent::EnterBlessingStep()
{
	// Roll this Aspect + slot's Blessings once per altar; going Back and choosing them again shows the same ones.
	const FString Key = FString::Printf(TEXT("%s|%d"), *GetPathNameSafe(ChosenAspect), static_cast<int32>(ChosenChannel));
	TArray<FFPSRLBoonOffer>* Rolled = RolledBlessings.Find(Key);
	if (!Rolled)
	{
		Rolled = &RolledBlessings.Add(Key, GenerateBlessingOptions(ChosenAspect, ChosenChannel));
	}
	if (Rolled->IsEmpty())
	{
		return false;
	}
	CurrentOptions = *Rolled;
	AltarStep = EFPSRLAltarStep::ChooseBlessing;
	LogOptions(TEXT("choose a Blessing"));
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TrySelect(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId)
		|| AltarStep != EFPSRLAltarStep::ChooseBlessing || !CurrentOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	const FFPSRLBoonOffer Offer = CurrentOptions[OptionIndex];
	if (!IsChannelOpen(Offer.Channel) || !IsEligible(Offer.Boon, Offer.Channel, false) || (Offer.bNewAspect && GetTrack(Offer.Channel).Aspect))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Blessings] %s: offer %s is no longer valid"), *GetNameSafe(GetOwner()), *GetNameSafe(Offer.Boon));
		return false;
	}

	EndSelection();	// resolve first, so nothing re-entrant can resolve this event twice
	ApplyBoon(Offer.Boon, Offer.Channel);
	const FFPSRLBoonTrack& Track = GetTrack(Offer.Channel);
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s took %s (%s) on %s (%s x%d, event %d)"), *GetNameSafe(GetOwner()), *GetNameSafe(Offer.Boon),
		*UEnum::GetDisplayValueAsText(Offer.Boon->BoonType).ToString(), *GetChannelName(Offer.Channel).ToString(), *GetNameSafe(Track.Aspect), Track.Count, EventId);
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TryBack(int32 EventId)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId))
	{
		return false;
	}
	if (AltarStep == EFPSRLAltarStep::ChooseBlessing && SlotOptions.Num() > 1)
	{
		AltarStep = EFPSRLAltarStep::ChooseSlot;	// a new Aspect with a slot to choose: back to the slot
	}
	else if (AltarStep == EFPSRLAltarStep::ChooseBlessing || AltarStep == EFPSRLAltarStep::ChooseSlot)
	{
		AltarStep = EFPSRLAltarStep::ChooseAspect;
		ChosenAspect = nullptr;
		SlotOptions.Reset();
	}
	else
	{
		return false;	// already at the first step
	}
	CurrentOptions.Reset();
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TryReroll(int32 EventId)
{
	AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS || !PS->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId) || AltarStep != EFPSRLAltarStep::ChooseAspect)
	{
		return false;	// rerolls replace the Aspect choices only
	}
	TArray<UFPSRLAspectDefinition*> NewOptions = GenerateAspectOptions(AspectOptions);
	if (NewOptions.IsEmpty())
	{
		return false;	// nothing else to offer; keep the current set and charge nothing
	}
	if (FreeRerollsRemaining > 0)
	{
		--FreeRerollsRemaining;
	}
	else if (!PS->TrySpendTalentEssence(UFPSRLBoonSettings::Get().RerollCost))
	{
		return false;
	}
	AspectOptions = NewOptions;
	LogOptions(TEXT("rerolled"));
	BroadcastChanged();
	return true;
}

// --- Upgrade Altar -----------------------------------------------------------------------------------------------

bool UFPSRLBoonComponent::BeginUpgradeSelection()
{
	if (!GetOwner()->HasAuthority() || HasPendingSelection())
	{
		return false;
	}
	TArray<FFPSRLUpgradeOffer> Options = GenerateUpgradeOptions();
	if (Options.IsEmpty())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: nothing to upgrade"), *GetNameSafe(GetOwner()));
		return false;
	}
	++SelectionEventId;
	PendingKind = EFPSRLBoonSelectionKind::Upgrade;
	UpgradeOptions = MoveTemp(Options);

	LogOptions(TEXT("upgrade selection opened"));
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TrySelectUpgrade(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Upgrade, EventId) || !UpgradeOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	const FFPSRLUpgradeOffer Offer = UpgradeOptions[OptionIndex];
	const FFPSRLBoonTrack& Track = GetTrack(Offer.Channel);
	const int32 OwnedIndex = Offer.Boon ? Track.Boons.IndexOfByPredicate([&Offer](const FFPSRLOwnedBoon& Owned) { return Owned.Boon == Offer.Boon; }) : INDEX_NONE;
	if (OwnedIndex == INDEX_NONE || Track.Boons[OwnedIndex].UpgradeLevel >= Offer.Boon->MaxUpgradeLevel)
	{
		return false;	// not owned any more, or already at its maximum
	}

	EndSelection();
	UpgradeBoon(Offer.Channel, OwnedIndex);
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s upgraded %s on %s to level %d/%d (event %d)"), *GetNameSafe(GetOwner()), *GetNameSafe(Offer.Boon),
		*GetChannelName(Offer.Channel).ToString(), Track.Boons[OwnedIndex].UpgradeLevel, Offer.Boon->MaxUpgradeLevel, EventId);
	BroadcastChanged();
	return true;
}

void UFPSRLBoonComponent::LogOptions(const TCHAR* What) const
{
	// One line per event, so a playtest log shows exactly what each player was offered.
	TArray<FString> Parts;
	if (PendingKind == EFPSRLBoonSelectionKind::Blessing && AltarStep == EFPSRLAltarStep::ChooseAspect)
	{
		for (const UFPSRLAspectDefinition* Aspect : AspectOptions)
		{
			const EFPSRLBoonChannel Channel = FindAspectChannel(Aspect);
			Parts.Add(FString::Printf(TEXT("%s (%s)"), *GetNameSafe(Aspect), Channel == EFPSRLBoonChannel::MAX ? TEXT("new") : *GetChannelName(Channel).ToString()));
		}
	}
	else if (PendingKind == EFPSRLBoonSelectionKind::Blessing && AltarStep == EFPSRLAltarStep::ChooseSlot)
	{
		for (const EFPSRLBoonChannel Channel : SlotOptions)
		{
			Parts.Add(FString::Printf(TEXT("%s for %s"), *GetChannelName(Channel).ToString(), *GetNameSafe(ChosenAspect)));
		}
	}
	else
	{
		for (const FFPSRLBoonOffer& Offer : CurrentOptions)
		{
			Parts.Add(FString::Printf(TEXT("%s %s [%s]%s"), *GetChannelName(Offer.Channel).ToString(), *GetNameSafe(Offer.Boon),
				Offer.Boon ? *UEnum::GetDisplayValueAsText(Offer.Boon->BoonType).ToString() : TEXT("?"), Offer.bNewAspect ? TEXT(" (new Aspect)") : TEXT("")));
		}
	}
	for (const FFPSRLUpgradeOffer& Offer : UpgradeOptions)
	{
		Parts.Add(FString::Printf(TEXT("%s %s"), *GetChannelName(Offer.Channel).ToString(), *GetNameSafe(Offer.Boon)));
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: %s (event %d): %s"), *GetNameSafe(GetOwner()), What, SelectionEventId, *FString::Join(Parts, TEXT(" | ")));
}

void UFPSRLBoonComponent::EndSelection()
{
	PendingKind = EFPSRLBoonSelectionKind::None;
	AltarStep = EFPSRLAltarStep::ChooseAspect;
	AspectOptions.Reset();
	SlotOptions.Reset();
	ChosenAspect = nullptr;
	RolledBlessings.Reset();
	CurrentOptions.Reset();
	UpgradeOptions.Reset();
}

// --- Granting ----------------------------------------------------------------------------------------------------

bool UFPSRLBoonComponent::GrantBoon(UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel)
{
	if (!GetOwner()->HasAuthority() || !IsChannelOpen(Channel) || !IsEligible(Boon, Channel, false))
	{
		return false;
	}
	ApplyBoon(Boon, Channel);
	BroadcastChanged();
	return true;
}

void UFPSRLBoonComponent::ApplyBoon(UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel)
{
	FFPSRLBoonTrack& Track = GetMutableTrack(Channel);
	FChannelHandles& ChannelHandles = Handles[static_cast<int32>(Channel)];

	if (!Track.Aspect)
	{
		Track.Aspect = Boon->Aspect;	// the first Blessing establishes the channel's Aspect
		GiveAspect(Channel);
	}

	int32 OwnedIndex = Track.Boons.IndexOfByPredicate([Boon](const FFPSRLOwnedBoon& Owned) { return Owned.Boon == Boon; });
	if (OwnedIndex == INDEX_NONE)
	{
		OwnedIndex = Track.Boons.Add({ Boon, 0, 0 });
		ChannelHandles.Boons.SetNum(Track.Boons.Num());
	}
	GiveBoonStack(Channel, OwnedIndex, Track.Boons[OwnedIndex].Stacks == 0);
	++Track.Boons[OwnedIndex].Stacks;
	++Track.Count;
	RefreshAllBlessingsChosen();
}

void UFPSRLBoonComponent::GiveAspect(EFPSRLBoonChannel Channel)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const FFPSRLBoonTrack& Track = GetTrack(Channel);
	if (!ASC || !Track.Aspect)
	{
		return;
	}
	FFPSRLGrantSet ToGrant;
	if (const FFPSRLGrantSet* Grants = Track.Aspect->GetGrants(Channel))
	{
		ToGrant = *Grants;
	}
	if (Track.Aspect->AspectTag.IsValid())
	{
		ToGrant.GrantedTags.AddTag(Track.Aspect->AspectTag);	// identity tag while a channel holds it
	}
	Handles[static_cast<int32>(Channel)].Aspect = FPSRLGrants::Give(ToGrant, ASC, Track.Aspect, FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run));
}

void UFPSRLBoonComponent::GiveBoonStack(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const FFPSRLOwnedBoon& Owned = GetTrack(Channel).Boons[OwnedIndex];
	if (!ASC || !Owned.Boon)
	{
		return;
	}
	// First stack: effects, abilities, tags, cue. Extra stacks re-apply only the effects (numeric stacking).
	FFPSRLGrantSet ToGrant = Owned.Boon->Grants;
	if (!bFirstStack)
	{
		ToGrant.Abilities.Reset();
		ToGrant.GrantedTags.Reset();
		ToGrant.GrantCue = FGameplayTag();
	}
	Handles[static_cast<int32>(Channel)].Boons[OwnedIndex].Add(
		FPSRLGrants::Give(ToGrant, ASC, Owned.Boon, FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run)));

	// A stack taken (or restored) after upgrades gets them too.
	for (int32 Level = 0; Level < Owned.UpgradeLevel; ++Level)
	{
		GiveBoonUpgrade(Channel, OwnedIndex, bFirstStack);
	}
}

void UFPSRLBoonComponent::GiveBoonUpgrade(EFPSRLBoonChannel Channel, int32 OwnedIndex, bool bFirstStack)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const FFPSRLOwnedBoon& Owned = GetTrack(Channel).Boons[OwnedIndex];
	if (!ASC || !Owned.Boon || FPSRLGrants::IsEmpty(Owned.Boon->UpgradedGrants))
	{
		return;
	}
	FFPSRLGrantSet ToGrant = Owned.Boon->UpgradedGrants;
	if (!bFirstStack)
	{
		ToGrant.Abilities.Reset();
		ToGrant.GrantedTags.Reset();
		ToGrant.GrantCue = FGameplayTag();
	}
	Handles[static_cast<int32>(Channel)].Boons[OwnedIndex].Add(
		FPSRLGrants::Give(ToGrant, ASC, Owned.Boon, FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run)));
}

void UFPSRLBoonComponent::UpgradeBoon(EFPSRLBoonChannel Channel, int32 OwnedIndex)
{
	// Upgrades stack: each level adds UpgradedGrants on top of everything already granted, once per stack.
	// The channel's count is untouched.
	FFPSRLOwnedBoon& Owned = GetMutableTrack(Channel).Boons[OwnedIndex];
	++Owned.UpgradeLevel;
	for (int32 Stack = 0; Stack < Owned.Stacks; ++Stack)
	{
		GiveBoonUpgrade(Channel, OwnedIndex, Stack == 0);
	}
}

// --- Run lifetime ------------------------------------------------------------------------------------------------

void UFPSRLBoonComponent::ClearRunState()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	for (FChannelHandles& ChannelHandles : Handles)
	{
		FPSRLGrants::Take(ChannelHandles.Aspect, ASC);
		for (TArray<FFPSRLGrantHandles>& Stacks : ChannelHandles.Boons)
		{
			for (FFPSRLGrantHandles& Stack : Stacks)
			{
				FPSRLGrants::Take(Stack, ASC);
			}
		}
		ChannelHandles = FChannelHandles();
	}
	for (FFPSRLBoonTrack& Track : Tracks)
	{
		const EFPSRLBoonChannel Channel = Track.Channel;
		Track = FFPSRLBoonTrack();
		Track.Channel = Channel;
	}
	ProcStates.Reset();	// the run is over: proc progress goes with it
	PendingRestore.Reset();
	EndSelection();
	FreeRerollsRemaining = UFPSRLBoonSettings::Get().FreeRerollsPerRun;	// the run is over: free rerolls come back
	bAllBlessingsChosen = false;
	BroadcastChanged();
}

void UFPSRLBoonComponent::CopyRunStateTo(UFPSRLBoonComponent* Other) const
{
	// The new PlayerState has a fresh ASC: hand over the build and it re-grants on BeginRunState. An open choice is
	// not carried: leaving the Depth forfeits it. A build not yet restored here (two travels in a row) passes on as is.
	if (Other)
	{
		Other->PendingRestore = PendingRestore.IsEmpty() ? Tracks : PendingRestore;
		Other->FreeRerollsRemaining = FreeRerollsRemaining;	// free rerolls are per run, not per Depth
		// Proc progress (stacks, counters) belongs to the run; cooldowns restart (the new level has its own clock).
		Other->ProcStates = ProcStates;
		for (TPair<uint32, FPSRLProcs::FProcState>& Entry : Other->ProcStates)
		{
			Entry.Value.LastProcTime = -1.0e9;
			Entry.Value.LastAttackId = INDEX_NONE;
		}
	}
}

void UFPSRLBoonComponent::RestoreRunState()
{
	if (!GetOwner()->HasAuthority() || PendingRestore.IsEmpty() || !GetAbilitySystem())
	{
		return;
	}

	const TArray<FFPSRLBoonTrack> ToRestore = MoveTemp(PendingRestore);
	PendingRestore.Reset();
	int32 Restored = 0;
	for (const FFPSRLBoonTrack& Carried : ToRestore)
	{
		const EFPSRLBoonChannel Channel = Carried.Channel;
		GetMutableTrack(Channel) = Carried;
		FChannelHandles& ChannelHandles = Handles[static_cast<int32>(Channel)];
		ChannelHandles = FChannelHandles();
		GiveAspect(Channel);

		ChannelHandles.Boons.SetNum(Carried.Boons.Num());
		for (int32 OwnedIndex = 0; OwnedIndex < Carried.Boons.Num(); ++OwnedIndex)
		{
			for (int32 Stack = 0; Stack < Carried.Boons[OwnedIndex].Stacks; ++Stack)
			{
				GiveBoonStack(Channel, OwnedIndex, Stack == 0);
				++Restored;
			}
		}
	}
	RefreshAllBlessingsChosen();
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: restored %d Blessing stack(s) after travel"), *GetNameSafe(GetOwner()), Restored);
	BroadcastChanged();
}

#undef LOCTEXT_NAMESPACE

EFPSRLItemSource UFPSRLBoonComponent::GetChannelSource(EFPSRLBoonChannel Channel) const
{
	// The most specific listed tag wins (Weapon.Rifle over Weapon).
	const FGameplayTag Item = GetChannelItem(Channel);
	int32 BestDepth = -1;
	EFPSRLItemSource Source = Channel == EFPSRLBoonChannel::Primary ? EFPSRLItemSource::Ranged
		: Channel == EFPSRLBoonChannel::Secondary ? EFPSRLItemSource::Melee : EFPSRLItemSource::Ability;
	if (Item.IsValid())
	{
		for (const TPair<FGameplayTag, EFPSRLItemSource>& Entry : UFPSRLBoonSettings::Get().ItemSources)
		{
			if (Item.MatchesTag(Entry.Key))
			{
				int32 Depth = 0;
				for (const TCHAR* Char = *Entry.Key.ToString(); *Char; ++Char)
				{
					Depth += *Char == TEXT('.') ? 1 : 0;
				}
				if (Depth > BestDepth)
				{
					BestDepth = Depth;
					Source = Entry.Value;
				}
			}
		}
	}
	return Source;
}


// --- Blessing behaviour: conditional damage and triggers (server) ---------------------------------------------------

namespace FPSRLBoons
{
	/** The hit enemy's tags meet the requirements (no requirements = always). */
	bool TargetMeets(const FGameplayTagRequirements& Requirements, const AActor* Target)
	{
		if (Requirements.IsEmpty())
		{
			return true;
		}
		const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		return TargetASC && Requirements.RequirementsMet(TargetASC->GetOwnedGameplayTags());
	}
}

void UFPSRLBoonComponent::BeginPlay()
{
	Super::BeginPlay();
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (ASC && GetOwner()->HasAuthority())
	{
		// Every combat event (Event.Hit, Event.Kill, anything under Event) reaches the Blessings' triggers.
		CombatEventHandle = ASC->AddGameplayEventTagContainerDelegate(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Event"))),
			FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleCombatEvent));
	}
}

void UFPSRLBoonComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystem(); ASC && CombatEventHandle.IsValid())
	{
		ASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Event"))), CombatEventHandle);
	}
	if (UWorld* World = GetWorld())
	{
		for (TPair<uint32, FTimerHandle>& Timer : PeriodicTimers)
		{
			World->GetTimerManager().ClearTimer(Timer.Value);
		}
	}
	PeriodicTimers.Reset();
	Super::EndPlay(EndPlayReason);
}

void UFPSRLBoonComponent::ModifyOutgoingDamage(const FPSRLCombat::FPlayerHit& Hit, AActor* Target, float& InOutDamage) const
{
	// Only the Blessings on slots whose item made this kind of attack (Fire on the gun boosts gun hits, not melee).
	float Bonus = 0.f;
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		if (Track.Boons.IsEmpty() || GetChannelSource(Track.Channel) != Hit.Source)
		{
			continue;
		}
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			for (const FFPSRLBlessingDamageBonus& Entry : Owned.Boon ? Owned.Boon->DamageBonuses : TArray<FFPSRLBlessingDamageBonus>())
			{
				if ((!Entry.bCriticalOnly || Hit.bCritical) && FPSRLBoons::TargetMeets(Entry.TargetRequirements, Target))
				{
					Bonus += (Entry.Bonus + Entry.BonusPerUpgrade * Owned.UpgradeLevel) * FMath::Max(1, Owned.Stacks);
				}
			}
		}
	}
	InOutDamage *= FMath::Max(0.f, 1.f + Bonus);
}

uint32 UFPSRLBoonComponent::ProcKey(const UFPSRLBoonDefinition* Boon, int32 TriggerIndex, EFPSRLBoonChannel Channel)
{
	return HashCombine(HashCombine(GetTypeHash(Boon), GetTypeHash(TriggerIndex)), GetTypeHash(Channel));
}

bool UFPSRLBoonComponent::ClaimAttack(EFPSRLItemSource Source, int32 AttackId)
{
	int32& Last = LastClaimedAttack[FMath::Clamp(static_cast<int32>(Source), 0, 2)];
	if (AttackId != INDEX_NONE && AttackId == Last)
	{
		return false;
	}
	Last = AttackId;
	return true;
}

float UFPSRLBoonComponent::GetEventInterval(EFPSRLItemSource Source, bool bPerHit) const
{
	const AFPSRLPlayerState* State = GetOwningPlayerState();
	APawn* Pawn = State ? State->GetPawn() : nullptr;
	if (!Pawn)
	{
		return 0.f;
	}
	switch (Source)
	{
	case EFPSRLItemSource::Ranged:
		// The held gun (weapons are local actors on every machine, so the server has this player's too).
		for (TActorIterator<AFPSRLWeapon> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == Pawn && !It->IsHidden())
			{
				const float Refire = It->GetRefireRate();	// already divided by the player's current fire rate
				return bPerHit ? Refire / FMath::Max(1, It->ProjectilesPerShot) : Refire;
			}
		}
		return 0.f;
	case EFPSRLItemSource::Melee:
		if (const AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Pawn->GetController()))
		{
			return PC->GetEffectiveMeleeCooldown();	// the current melee speed
		}
		return 0.f;
	default:
		return 0.f;	// abilities: no cast rate yet (the trigger's FallbackInterval applies)
	}
}

const FPSRLProcs::FProcState* UFPSRLBoonComponent::FindProcState(const UFPSRLBoonDefinition* Boon, int32 TriggerIndex, EFPSRLBoonChannel Channel) const
{
	return ProcStates.Find(ProcKey(Boon, TriggerIndex, Channel));
}

float UFPSRLBoonComponent::GetCurrentProcChance(const UFPSRLBoonDefinition* Boon, int32 TriggerIndex, EFPSRLBoonChannel Channel) const
{
	if (!Boon || !Boon->Triggers.IsValidIndex(TriggerIndex))
	{
		return 0.f;
	}
	const FFPSRLBlessingTrigger& Trigger = Boon->Triggers[TriggerIndex];
	const FFPSRLOwnedBoon* Owned = GetTrack(Channel).Boons.FindByPredicate([Boon](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Boon; });
	FPSRLProcs::FProcEvent Event;
	Event.UpgradeLevel = Owned ? Owned->UpgradeLevel : 0;
	Event.EventInterval = GetEventInterval(GetChannelSource(Channel), !Trigger.Event.MatchesTagExact(FPSRLGameplayTags::Event_Attack) && Trigger.Scope == EFPSRLProcScope::EachEvent);
	return FPSRLProcs::GetChance(Trigger, Event);
}

void UFPSRLBoonComponent::HandleCombatEvent(FGameplayTag EventTag, const FGameplayEventData* Payload)
{
	if (!Payload || !GetOwner()->HasAuthority())
	{
		return;
	}
	const AActor* Target = Payload->Target.Get();
	const bool bAttackEvent = EventTag.MatchesTagExact(FPSRLGameplayTags::Event_Attack);
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		// A slot's Blessings react to that slot's own source (events without a source reach every slot).
		const EFPSRLItemSource SlotSource = GetChannelSource(Track.Channel);
		if (Track.Boons.IsEmpty() || (Payload->InstigatorTags.HasTag(FGameplayTag::RequestGameplayTag(TEXT("Source")))
			&& !Payload->InstigatorTags.HasTagExact(FPSRLCombat::GetSourceTag(SlotSource))))
		{
			continue;
		}
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			for (int32 Index = 0; Owned.Boon && Index < Owned.Boon->Triggers.Num(); ++Index)
			{
				const FFPSRLBlessingTrigger& Trigger = Owned.Boon->Triggers[Index];
				if (Trigger.Model == EFPSRLProcModel::Periodic || !EventTag.MatchesTag(Trigger.Event) || !FPSRLBoons::TargetMeets(Trigger.TargetRequirements, Target))
				{
					continue;
				}
				FPSRLProcs::FProcEvent Event;
				Event.Now = GetWorld()->GetTimeSeconds();
				Event.AttackId = EventAttackId;
				Event.Damage = bAttackEvent ? 0.f : Payload->EventMagnitude;
				Event.bCritical = Payload->TargetTags.HasTag(FPSRLGameplayTags::Hit_Critical);
				Event.UpgradeLevel = Owned.UpgradeLevel;
				if (Trigger.Model == EFPSRLProcModel::Normalized)
				{
					Event.EventInterval = GetEventInterval(SlotSource, !bAttackEvent && Trigger.Scope == EFPSRLProcScope::EachEvent);
				}
				FPSRLProcs::FProcState& State = ProcStates.FindOrAdd(ProcKey(Owned.Boon, Index, Track.Channel));
				if (!FPSRLProcs::Evaluate(Trigger, State, Event))
				{
					continue;
				}
				UE_LOG(LogFPSRL, Verbose, TEXT("[Blessings] %s proc %d on %s (%s)"), *Owned.Boon->GetName(), State.ProcCount, *EventTag.ToString(), *GetNameSafe(Target));
				for (const FFPSRLBlessingAction& Action : Trigger.Actions)
				{
					RunBlessingAction(Action, Owned, *Payload);
				}
			}
		}
	}
}

void UFPSRLBoonComponent::RefreshPeriodicProcs()
{
	UWorld* World = GetWorld();
	if (!World || !GetOwner()->HasAuthority())
	{
		return;
	}
	TSet<uint32> Wanted;
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			for (int32 Index = 0; Owned.Boon && Index < Owned.Boon->Triggers.Num(); ++Index)
			{
				const FFPSRLBlessingTrigger& Trigger = Owned.Boon->Triggers[Index];
				if (Trigger.Model != EFPSRLProcModel::Periodic)
				{
					continue;
				}
				const uint32 Key = ProcKey(Owned.Boon, Index, Track.Channel);
				Wanted.Add(Key);
				if (!PeriodicTimers.Contains(Key))
				{
					FTimerHandle& Handle = PeriodicTimers.Add(Key);
					TWeakObjectPtr<const UFPSRLBoonDefinition> Boon = Owned.Boon.Get();
					const EFPSRLBoonChannel Channel = Track.Channel;
					World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Boon, Index, Channel]()
					{
						RunPeriodicProc(Boon.Get(), Index, Channel);
					}), FMath::Max(0.05f, Trigger.Interval), true);
				}
			}
		}
	}
	for (auto It = PeriodicTimers.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It.Key()))
		{
			World->GetTimerManager().ClearTimer(It.Value());
			It.RemoveCurrent();
		}
	}
}

void UFPSRLBoonComponent::RunPeriodicProc(const UFPSRLBoonDefinition* Boon, int32 TriggerIndex, EFPSRLBoonChannel Channel)
{
	const FFPSRLOwnedBoon* Owned = Boon ? GetTrack(Channel).Boons.FindByPredicate([Boon](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Boon; }) : nullptr;
	const AFPSRLPlayerState* State = GetOwningPlayerState();
	if (!Owned || !Boon->Triggers.IsValidIndex(TriggerIndex) || !State || !UFPSRLHealthComponent::IsPawnUp(State->GetPawn()))
	{
		return;	// gone, or the player is downed / dead
	}
	const FFPSRLBlessingTrigger& Trigger = Boon->Triggers[TriggerIndex];
	FPSRLProcs::FProcEvent Event;
	Event.Now = GetWorld()->GetTimeSeconds();
	Event.UpgradeLevel = Owned->UpgradeLevel;
	if (!FPSRLProcs::Evaluate(Trigger, ProcStates.FindOrAdd(ProcKey(Boon, TriggerIndex, Channel)), Event))
	{
		return;
	}
	FGameplayEventData Payload;
	Payload.Instigator = State->GetPawn();
	for (const FFPSRLBlessingAction& Action : Trigger.Actions)
	{
		RunBlessingAction(Action, *Owned, Payload);
	}
}

void UFPSRLBoonComponent::RunBlessingAction(const FFPSRLBlessingAction& Action, const FFPSRLOwnedBoon& Owned, const FGameplayEventData& Payload) const
{
	UAbilitySystemComponent* SourceASC = GetAbilitySystem();
	if (!SourceASC || !Action.Effect)
	{
		return;
	}
	AActor* HitTarget = const_cast<AActor*>(Payload.Target.Get());
	AActor* Self = SourceASC->GetAvatarActor();

	TArray<AActor*> Recipients;
	switch (Action.Target)
	{
	case EFPSRLBlessingActionTarget::HitTarget:
		Recipients.Add(HitTarget);
		break;
	case EFPSRLBlessingActionTarget::Self:
		Recipients.Add(Self);
		break;
	default:
	{
		// Every enemy (never a player) within the radius of the hit enemy or the player.
		const AActor* Center = Action.Target == EFPSRLBlessingActionTarget::AreaAroundTarget ? HitTarget : Self;
		if (!Center || !GetWorld())
		{
			break;
		}
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			const APlayerState* PawnState = It->GetPlayerState();
			if ((!PawnState || PawnState->IsABot()) && FVector::DistSquared(It->GetActorLocation(), Center->GetActorLocation()) <= FMath::Square(Action.Radius))
			{
				Recipients.Add(*It);
			}
		}
		break;
	}
	}

	const float Magnitude = (Action.Magnitude + Action.MagnitudePerUpgrade * Owned.UpgradeLevel + Action.HitDamageFraction * Payload.EventMagnitude)
		* FMath::Max(1, Owned.Stacks);
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(Self, Self);
	const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(Action.Effect, Owned.UpgradeLevel + 1, Context);
	if (!Spec.IsValid())
	{
		return;
	}
	if (Action.MagnitudeTag.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(Action.MagnitudeTag, Magnitude);
	}
	for (AActor* Recipient : Recipients)
	{
		if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Recipient))
		{
			SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
		}
	}
}
