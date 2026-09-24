// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLBoonComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Attributes/FPSRLProgressionSet.h"
#include "Components/FPSRLAspectComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

UFPSRLBoonComponent::UFPSRLBoonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSRLBoonComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFPSRLBoonComponent, OwnedBoons);	// everyone may show a teammate's build
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, CurrentOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, SelectionEventId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, bSelectionPending, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLBoonComponent, SelectionDeadline, COND_OwnerOnly);
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

int32 UFPSRLBoonComponent::GetStacks(const UFPSRLBoonDefinition* Boon) const
{
	const FFPSRLOwnedBoon* Owned = OwnedBoons.FindByPredicate([Boon](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Boon; });
	return Owned ? Owned->Stacks : 0;
}

int32 UFPSRLBoonComponent::GetUsedBoonSlots() const
{
	int32 Used = 0;
	for (const FFPSRLOwnedBoon& Entry : OwnedBoons)
	{
		Used += Entry.Stacks;
	}
	return Used;
}

int32 UFPSRLBoonComponent::GetMaxBoonSlots() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystem();
	bool bFound = false;
	const float Value = ASC ? ASC->GetGameplayAttributeValue(UFPSRLProgressionSet::GetMaxBoonSlotsAttribute(), bFound) : 0.f;
	return bFound ? FMath::RoundToInt(Value) : 20;
}

FGameplayTagContainer UFPSRLBoonComponent::GetOwnedElements() const
{
	FGameplayTagContainer Elements;
	for (const FFPSRLOwnedBoon& Entry : OwnedBoons)
	{
		if (Entry.Boon)
		{
			Elements.AppendTags(Entry.Boon->ElementTags);
		}
	}
	return Elements;
}

int32 UFPSRLBoonComponent::GetNextRerollCost() const
{
	return FreeRerollsRemaining > 0 ? 0 : UFPSRLBoonSettings::Get().RerollCost;
}

FGameplayTagContainer UFPSRLBoonComponent::GetBuildTags() const
{
	FGameplayTagContainer Tags;
	if (const AFPSRLPlayerState* PS = GetOwningPlayerState())
	{
		if (PS->SelectedWeapon.IsValid())
		{
			Tags.AddTag(PS->SelectedWeapon);
		}
	}
	if (const UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		Tags.AppendTags(ASC->GetOwnedGameplayTags());	// aspect + build tags granted through GAS
	}
	Tags.AppendTags(GetOwnedElements());
	return Tags;
}

// --- Generation ----------------------------------------------------------------------------------------------------

bool UFPSRLBoonComponent::IsEligible(const UFPSRLBoonDefinition* Boon, bool bForReroll) const
{
	if (!Boon || Boon->SelectionWeight <= 0.f || (bForReroll && !Boon->bCanReroll))
	{
		return false;
	}

	// Per-boon stacking rules.
	const int32 Stacks = GetStacks(Boon);
	if (Stacks >= Boon->GetMaxStacks() || (Stacks > 0 && !Boon->bCanAppearAfterOwned))
	{
		return false;
	}

	const AFPSRLPlayerState* PS = GetOwningPlayerState();
	const FGameplayTagContainer BuildTags = GetBuildTags();

	if (!Boon->RequiredWeaponTags.IsEmpty() && !(PS && Boon->RequiredWeaponTags.HasTagExact(PS->SelectedWeapon)))
	{
		return false;
	}
	if (!Boon->RequiredAspectTags.IsEmpty() && !BuildTags.HasAny(Boon->RequiredAspectTags))
	{
		return false;
	}

	const FGameplayTagContainer OwnedElements = GetOwnedElements();
	if (!Boon->RequiredElementTags.IsEmpty() && !OwnedElements.HasAnyExact(Boon->RequiredElementTags))
	{
		return false;
	}
	if (BuildTags.HasAny(Boon->BlockedTags))
	{
		return false;
	}
	for (const UFPSRLBoonDefinition* Prerequisite : Boon->RequiredBoons)
	{
		if (Prerequisite && GetStacks(Prerequisite) == 0)
		{
			return false;
		}
	}

	// The active aspect can exclude boons by tag.
	if (const UFPSRLAspectComponent* Aspects = PS ? PS->GetAspectComponent() : nullptr)
	{
		if (Aspects->ActiveAspect && Boon->BoonTags.HasAny(Aspects->ActiveAspect->RestrictedBoonTags))
		{
			return false;
		}
	}

	// Element cap: taking this boon must not push the player past MaxElementsPerRun distinct elements.
	int32 NewElements = 0;
	for (const FGameplayTag& Element : Boon->ElementTags)
	{
		if (!OwnedElements.HasTagExact(Element))
		{
			++NewElements;
		}
	}
	return OwnedElements.Num() + NewElements <= UFPSRLBoonSettings::Get().MaxElementsPerRun;
}

float UFPSRLBoonComponent::GetOfferWeight(const UFPSRLBoonDefinition* Boon) const
{
	// Base weight x (1 + bonus per owned boon of each of this boon's elements). No rarity involved.
	int32 OwnedOfSameElement = 0;
	for (const FFPSRLOwnedBoon& Entry : OwnedBoons)
	{
		if (Entry.Boon && Entry.Boon->ElementTags.HasAnyExact(Boon->ElementTags))
		{
			OwnedOfSameElement += Entry.Stacks;
		}
	}
	return Boon->SelectionWeight * (1.f + UFPSRLBoonSettings::Get().ElementWeightBonusPerOwnedBoon * OwnedOfSameElement);
}

TArray<TObjectPtr<UFPSRLBoonDefinition>> UFPSRLBoonComponent::GenerateOptions(bool bForReroll) const
{
	TArray<TObjectPtr<UFPSRLBoonDefinition>> Options;
	if (GetUsedBoonSlots() >= GetMaxBoonSlots())
	{
		return Options;
	}

	const UFPSRLBoonPool* Pool = UFPSRLBoonSettings::Get().BoonPool.LoadSynchronous();
	if (!Pool)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("No Boon Pool set in Project Settings > FPSRL Boons"));
		return Options;
	}

	TArray<UFPSRLBoonDefinition*> Candidates;
	TArray<float> Weights;
	for (UFPSRLBoonDefinition* Boon : Pool->Boons)
	{
		if (IsEligible(Boon, bForReroll) && !Candidates.Contains(Boon))
		{
			Candidates.Add(Boon);
			Weights.Add(GetOfferWeight(Boon));
		}
	}

	// Weighted picks without replacement: the same boon never appears twice in one set.
	const int32 Count = UFPSRLBoonSettings::Get().BoonOptionsPerSelection;
	while (Options.Num() < Count && !Candidates.IsEmpty())
	{
		float Total = 0.f;
		for (const float Weight : Weights)
		{
			Total += Weight;
		}
		float Roll = FMath::FRandRange(0.f, Total);
		int32 Chosen = Candidates.Num() - 1;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Roll -= Weights[Index];
			if (Roll <= 0.f)
			{
				Chosen = Index;
				break;
			}
		}
		Options.Add(Candidates[Chosen]);
		Candidates.RemoveAtSwap(Chosen);
		Weights.RemoveAtSwap(Chosen);
	}
	return Options;
}

// --- Selection -----------------------------------------------------------------------------------------------------

bool UFPSRLBoonComponent::BeginSelection(int32 EventId, double Deadline)
{
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	SelectionEventId = EventId;
	SelectionDeadline = Deadline;
	FreeRerollsRemaining = UFPSRLBoonSettings::Get().FreeRerollsPerSelection;
	CurrentOptions = GenerateOptions(false);
	bSelectionPending = !CurrentOptions.IsEmpty();

	UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s: selection %d opened with %d option(s)"),
		*GetNameSafe(GetOwner()), EventId, CurrentOptions.Num());
	BroadcastChanged();
	return bSelectionPending;
}

bool UFPSRLBoonComponent::TrySelect(int32 EventId, int32 OptionIndex)
{
	if (!GetOwner()->HasAuthority() || !IsSelectionPending(EventId) || !CurrentOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	const AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>();
	if (!GameState || !GameState->IsBoonSelectionEventActive(EventId))
	{
		return false;
	}

	ResolveSelection(CurrentOptions[OptionIndex], TEXT("picked"));
	return true;
}

bool UFPSRLBoonComponent::TryReroll(int32 EventId)
{
	AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS || !PS->HasAuthority() || !IsSelectionPending(EventId))
	{
		return false;
	}

	TArray<TObjectPtr<UFPSRLBoonDefinition>> NewOptions = GenerateOptions(true);
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
	BroadcastChanged();
	return true;
}

void UFPSRLBoonComponent::AutoSelect(int32 EventId)
{
	if (GetOwner()->HasAuthority() && IsSelectionPending(EventId) && !CurrentOptions.IsEmpty())
	{
		ResolveSelection(CurrentOptions[FMath::RandRange(0, CurrentOptions.Num() - 1)], TEXT("auto-picked on timeout"));
	}
}

void UFPSRLBoonComponent::ForceResolve(int32 EventId)
{
	if (GetOwner()->HasAuthority() && IsSelectionPending(EventId))
	{
		bSelectionPending = false;
		CurrentOptions.Reset();
		BroadcastChanged();
	}
}

void UFPSRLBoonComponent::ResolveSelection(UFPSRLBoonDefinition* Chosen, const TCHAR* How)
{
	// Resolve first, so nothing re-entrant can resolve this event a second time.
	bSelectionPending = false;
	CurrentOptions.Reset();

	AddBoonStack(Chosen);
	UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s %s %s (event %d)"), *GetNameSafe(GetOwner()), How, *GetNameSafe(Chosen), SelectionEventId);
	BroadcastChanged();

	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->NotifyBoonSelectionResolved(SelectionEventId);
	}
}

bool UFPSRLBoonComponent::GrantBoon(UFPSRLBoonDefinition* Boon)
{
	if (!GetOwner()->HasAuthority() || !Boon || GetUsedBoonSlots() >= GetMaxBoonSlots() || GetStacks(Boon) >= Boon->GetMaxStacks())
	{
		return false;
	}
	const bool bAdded = AddBoonStack(Boon);
	BroadcastChanged();
	return bAdded;
}

bool UFPSRLBoonComponent::AddBoonStack(UFPSRLBoonDefinition* Boon)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!Boon || !ASC)
	{
		return false;
	}

	int32 Index = OwnedBoons.IndexOfByPredicate([Boon](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Boon; });
	const bool bFirstStack = Index == INDEX_NONE;
	if (bFirstStack)
	{
		Index = OwnedBoons.Add({ Boon, 0 });
		OwnedBoonHandles.SetNum(OwnedBoons.Num());
	}

	// First stack: effects, abilities, tags, grant cue. Extra stacks re-apply only the effects (numeric stacking).
	FFPSRLGrantSet ToGrant = Boon->Grants;
	if (!bFirstStack)
	{
		ToGrant.Abilities.Reset();
		ToGrant.GrantedTags.Reset();
		ToGrant.GrantCue = FGameplayTag();
	}

	FGameplayTagContainer SpecTags(FPSRLGameplayTags::Effect_Temporary_Run);
	OwnedBoonHandles[Index].Add(FPSRLGrants::Give(ToGrant, ASC, Boon, SpecTags));
	++OwnedBoons[Index].Stacks;
	return true;
}

void UFPSRLBoonComponent::ClearRunState()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystem();
	for (TArray<FFPSRLGrantHandles>& Stacks : OwnedBoonHandles)
	{
		for (FFPSRLGrantHandles& Handles : Stacks)
		{
			FPSRLGrants::Take(Handles, ASC);
		}
	}
	OwnedBoonHandles.Reset();
	OwnedBoons.Reset();
	CurrentOptions.Reset();
	bSelectionPending = false;
	FreeRerollsRemaining = 0;
	BroadcastChanged();
}
