// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLBoonComponent.h"
#include "AbilitySystemComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
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
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, CurrentOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, UpgradeOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, SelectionEventId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, FreeRerollsRemaining, COND_OwnerOnly);
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

TArray<FFPSRLBoonOffer> UFPSRLBoonComponent::GenerateOptions(bool bForReroll) const
{
	struct FCandidate
	{
		UFPSRLBoonDefinition* Boon = nullptr;
		EFPSRLBoonChannel Channel = EFPSRLBoonChannel::Primary;
		bool bNewAspect = false;
		bool bMilestone = false;
		float Weight = 1.f;
	};

	TArray<FFPSRLBoonOffer> Options;
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const UFPSRLBoonPool* Pool = Settings.BoonPool.LoadSynchronous();
	if (!Pool)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Blessings] No Blessing pool set in Project Settings > FPSRL Blessings"));
		return Options;
	}

	TArray<FCandidate> Candidates;
	for (const EFPSRLBoonChannel Channel : TEnumRange<EFPSRLBoonChannel>())
	{
		if (!IsChannelOpen(Channel))
		{
			continue;	// no item (e.g. no Ability yet) or all positions filled
		}
		const FFPSRLBoonTrack& Track = GetTrack(Channel);

		if (!Track.Aspect)
		{
			// Empty channel: one "new Aspect" choice per Aspect, shown through one of its first Blessings.
			TMap<const UFPSRLAspectDefinition*, TArray<UFPSRLBoonDefinition*>> ByAspect;
			for (UFPSRLBoonDefinition* Boon : Pool->Boons)
			{
				if (Boon && Boon->BoonType == EFPSRLBoonType::Normal && IsEligible(Boon, Channel, bForReroll))
				{
					ByAspect.FindOrAdd(Boon->Aspect).AddUnique(Boon);
				}
			}
			for (const TPair<const UFPSRLAspectDefinition*, TArray<UFPSRLBoonDefinition*>>& Entry : ByAspect)
			{
				const int32 Pick = FPSRLBoons::PickWeighted(Entry.Value, [](const UFPSRLBoonDefinition* Boon) { return Boon->SelectionWeight; });
				Candidates.Add({ Entry.Value[Pick], Channel, true, false, 1.f });
			}
			continue;
		}

		// Established channel: its Aspect only, and exactly the type its next position calls for.
		const EFPSRLBoonType Wanted = Settings.GetBoonTypeForPosition(Track.Count + 1);
		TArray<FCandidate> ChannelCandidates;
		for (UFPSRLBoonDefinition* Boon : Pool->Boons)
		{
			if (Boon && Boon->BoonType == Wanted && IsEligible(Boon, Channel, bForReroll))
			{
				ChannelCandidates.Add({ Boon, Channel, false, Wanted != EFPSRLBoonType::Normal, Boon->SelectionWeight });
			}
		}
		if (Wanted != EFPSRLBoonType::Normal && ChannelCandidates.IsEmpty())
		{
			// Content gap, not RNG: keep the channel progressing and say so.
			UE_LOG(LogFPSRL, Warning, TEXT("[Blessings] %s has no %s Blessing for %s at position %d; offering normal ones"),
				*GetNameSafe(Track.Aspect), *UEnum::GetValueAsString(Wanted), *GetChannelName(Channel).ToString(), Track.Count + 1);
			for (UFPSRLBoonDefinition* Boon : Pool->Boons)
			{
				if (Boon && Boon->BoonType == EFPSRLBoonType::Normal && IsEligible(Boon, Channel, bForReroll))
				{
					ChannelCandidates.Add({ Boon, Channel, false, false, Boon->SelectionWeight });
				}
			}
		}
		Candidates.Append(ChannelCandidates);
	}

	const int32 Count = Settings.BoonOptionsPerSelection;
	auto Take = [&Options, &Candidates](int32 Index)
	{
		const FCandidate Chosen = Candidates[Index];
		Options.Add({ Chosen.Boon, Chosen.Channel, Chosen.bNewAspect });
		// No Blessing twice in one set, and a channel's milestone appears once.
		Candidates.RemoveAll([&Chosen](const FCandidate& Other)
		{
			return Other.Boon == Chosen.Boon || (Chosen.bMilestone && Other.bMilestone && Other.Channel == Chosen.Channel);
		});
	};

	// Milestones are guaranteed: every channel at its Minor / Major position gets its milestone into the set.
	for (const EFPSRLBoonChannel Channel : TEnumRange<EFPSRLBoonChannel>())
	{
		TArray<int32> MilestoneIndices;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index].bMilestone && Candidates[Index].Channel == Channel)
			{
				MilestoneIndices.Add(Index);
			}
		}
		const int32 Pick = FPSRLBoons::PickWeighted(MilestoneIndices, [&Candidates](int32 Index) { return Candidates[Index].Weight; });
		if (Pick != INDEX_NONE && Options.Num() < Count)
		{
			Take(MilestoneIndices[Pick]);
		}
	}

	// The rest: weighted picks without replacement. No push toward filling empty slots: on every draw each open channel
	// has the same chance, however many candidates it still has (an empty channel has one per available Aspect, an
	// established one only its own Blessings); within a channel the Blessings' own weights decide. So specializing in
	// one channel stays exactly as likely as spreading out.
	while (Options.Num() < Count && !Candidates.IsEmpty())
	{
		TMap<EFPSRLBoonChannel, float> ChannelTotals;
		for (const FCandidate& Candidate : Candidates)
		{
			ChannelTotals.FindOrAdd(Candidate.Channel) += FMath::Max(0.f, Candidate.Weight);
		}
		Take(FPSRLBoons::PickWeighted(Candidates, [&ChannelTotals](const FCandidate& Candidate)
		{
			const float Total = ChannelTotals.FindRef(Candidate.Channel);
			return Total > 0.f ? FMath::Max(0.f, Candidate.Weight) / Total : 0.f;
		}));
	}

	FPSRLBoons::Shuffle(Options);
	return Options;
}

TArray<FFPSRLUpgradeOffer> UFPSRLBoonComponent::GenerateUpgradeOptions() const
{
	TArray<FFPSRLUpgradeOffer> Pool;
	for (const FFPSRLBoonTrack& Track : Tracks)
	{
		if (Track.Aspect && Track.AspectUpgradeLevel < Track.Aspect->MaxUpgradeLevel)
		{
			Pool.Add({ Track.Channel, nullptr });
		}
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

bool UFPSRLBoonComponent::BeginSelection()
{
	if (!GetOwner()->HasAuthority() || HasPendingSelection())
	{
		return false;	// one open choice at a time; the pending one stays as it is
	}

	TArray<FFPSRLBoonOffer> Options = GenerateOptions(false);
	if (Options.IsEmpty())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: nothing to offer"), *GetNameSafe(GetOwner()));
		return false;
	}
	++SelectionEventId;
	PendingKind = EFPSRLBoonSelectionKind::Blessing;
	FreeRerollsRemaining = UFPSRLBoonSettings::Get().FreeRerollsPerSelection;
	CurrentOptions = MoveTemp(Options);

	LogOptions(TEXT("selection opened"));
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TrySelect(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId) || !CurrentOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	const FFPSRLBoonOffer Offer = CurrentOptions[OptionIndex];
	if (!IsEligible(Offer.Boon, Offer.Channel, false) || (Offer.bNewAspect && GetTrack(Offer.Channel).Aspect))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Blessings] %s: offer %s is no longer valid"), *GetNameSafe(GetOwner()), *GetNameSafe(Offer.Boon));
		return false;
	}

	EndSelection();	// resolve first, so nothing re-entrant can resolve this event twice
	ApplyBoon(Offer.Boon, Offer.Channel);
	const FFPSRLBoonTrack& Track = GetTrack(Offer.Channel);
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s took %s on %s (%s x%d, event %d)"), *GetNameSafe(GetOwner()), *GetNameSafe(Offer.Boon),
		*GetChannelName(Offer.Channel).ToString(), *GetNameSafe(Track.Aspect), Track.Count, EventId);
	BroadcastChanged();
	return true;
}

bool UFPSRLBoonComponent::TryReroll(int32 EventId)
{
	AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS || !PS->HasAuthority() || !IsSelectionPending(EFPSRLBoonSelectionKind::Blessing, EventId))
	{
		return false;
	}

	TArray<FFPSRLBoonOffer> NewOptions = GenerateOptions(true);
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

	CurrentOptions = MoveTemp(NewOptions);
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

	if (!Offer.Boon)
	{
		if (!Track.Aspect || Track.AspectUpgradeLevel >= Track.Aspect->MaxUpgradeLevel)
		{
			return false;
		}
		EndSelection();
		UpgradeAspect(Offer.Channel);
	}
	else
	{
		const int32 OwnedIndex = Track.Boons.IndexOfByPredicate([&Offer](const FFPSRLOwnedBoon& Owned) { return Owned.Boon == Offer.Boon; });
		if (OwnedIndex == INDEX_NONE || Track.Boons[OwnedIndex].UpgradeLevel >= Offer.Boon->MaxUpgradeLevel)
		{
			return false;
		}
		EndSelection();
		UpgradeBoon(Offer.Channel, OwnedIndex);
	}

	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s upgraded %s on %s (event %d)"), *GetNameSafe(GetOwner()),
		Offer.Boon ? *GetNameSafe(Offer.Boon) : *GetNameSafe(Track.Aspect), *GetChannelName(Offer.Channel).ToString(), EventId);
	BroadcastChanged();
	return true;
}

void UFPSRLBoonComponent::LogOptions(const TCHAR* What) const
{
	// One line per event, so a playtest log shows exactly what each player was offered.
	TArray<FString> Parts;
	for (const FFPSRLBoonOffer& Offer : CurrentOptions)
	{
		Parts.Add(FString::Printf(TEXT("%s %s%s"), *GetChannelName(Offer.Channel).ToString(), *GetNameSafe(Offer.Boon), Offer.bNewAspect ? TEXT(" (new Aspect)") : TEXT("")));
	}
	for (const FFPSRLUpgradeOffer& Offer : UpgradeOptions)
	{
		Parts.Add(FString::Printf(TEXT("%s %s"), *GetChannelName(Offer.Channel).ToString(),
			Offer.Boon ? *GetNameSafe(Offer.Boon) : *FString::Printf(TEXT("%s Aspect"), *GetNameSafe(GetTrack(Offer.Channel).Aspect))));
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: %s (event %d): %s"), *GetNameSafe(GetOwner()), What, SelectionEventId, *FString::Join(Parts, TEXT(" | ")));
}

void UFPSRLBoonComponent::EndSelection()
{
	PendingKind = EFPSRLBoonSelectionKind::None;
	CurrentOptions.Reset();
	UpgradeOptions.Reset();
	FreeRerollsRemaining = 0;
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
	if (const FFPSRLGrantSet* Grants = Track.Aspect->GetGrants(Channel, Track.AspectUpgradeLevel))
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
	FFPSRLGrantSet ToGrant = Owned.Boon->GetGrants(Owned.UpgradeLevel);
	if (!bFirstStack)
	{
		ToGrant.Abilities.Reset();
		ToGrant.GrantedTags.Reset();
		ToGrant.GrantCue = FGameplayTag();
	}
	Handles[static_cast<int32>(Channel)].Boons[OwnedIndex].Add(
		FPSRLGrants::Give(ToGrant, ASC, Owned.Boon, FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run)));
}

void UFPSRLBoonComponent::UpgradeAspect(EFPSRLBoonChannel Channel)
{
	FPSRLGrants::Take(Handles[static_cast<int32>(Channel)].Aspect, GetAbilitySystem());
	++GetMutableTrack(Channel).AspectUpgradeLevel;
	GiveAspect(Channel);
}

void UFPSRLBoonComponent::UpgradeBoon(EFPSRLBoonChannel Channel, int32 OwnedIndex)
{
	// Swap every stack's grants for the upgraded ones. The channel's count is untouched.
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	TArray<FFPSRLGrantHandles>& StackHandles = Handles[static_cast<int32>(Channel)].Boons[OwnedIndex];
	for (FFPSRLGrantHandles& Stack : StackHandles)
	{
		FPSRLGrants::Take(Stack, ASC);
	}
	StackHandles.Reset();

	FFPSRLOwnedBoon& Owned = GetMutableTrack(Channel).Boons[OwnedIndex];
	++Owned.UpgradeLevel;
	for (int32 Stack = 0; Stack < Owned.Stacks; ++Stack)
	{
		GiveBoonStack(Channel, OwnedIndex, Stack == 0);
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
	PendingRestore.Reset();
	EndSelection();
	BroadcastChanged();
}

void UFPSRLBoonComponent::CopyRunStateTo(UFPSRLBoonComponent* Other) const
{
	// The new PlayerState has a fresh ASC: hand over the build and it re-grants on BeginRunState. An open choice is
	// not carried: leaving the Depth forfeits it. A build not yet restored here (two travels in a row) passes on as is.
	if (Other)
	{
		Other->PendingRestore = PendingRestore.IsEmpty() ? Tracks : PendingRestore;
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
	UE_LOG(LogFPSRL, Log, TEXT("[Blessings] %s: restored %d Blessing stack(s) after travel"), *GetNameSafe(GetOwner()), Restored);
	BroadcastChanged();
}

#undef LOCTEXT_NAMESPACE
