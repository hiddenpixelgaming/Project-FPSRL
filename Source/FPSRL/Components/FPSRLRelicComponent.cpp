// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLRelicComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Data/FPSRLBoonSettings.h"
#include "Data/FPSRLRelicDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

UFPSRLRelicComponent::UFPSRLRelicComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSRLRelicComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFPSRLRelicComponent, OwnedRelics);
}

void UFPSRLRelicComponent::OnRep_Relics()
{
	OnRelicsChanged.Broadcast();
}

void UFPSRLRelicComponent::BroadcastChanged()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnRelicsChanged.Broadcast();
}

UAbilitySystemComponent* UFPSRLRelicComponent::GetAbilitySystem() const
{
	const IAbilitySystemInterface* Owner = Cast<IAbilitySystemInterface>(GetOwner());
	return Owner ? Owner->GetAbilitySystemComponent() : nullptr;
}

int32 UFPSRLRelicComponent::GetStacks(const UFPSRLRelicDefinition* Relic) const
{
	const FFPSRLOwnedRelic* Owned = OwnedRelics.FindByPredicate([Relic](const FFPSRLOwnedRelic& Entry) { return Entry.Relic == Relic; });
	return Owned ? Owned->Stacks : 0;
}

bool UFPSRLRelicComponent::CanReceive(const UFPSRLRelicDefinition* Relic) const
{
	if (!Relic || GetStacks(Relic) >= Relic->GetMaxStacks())
	{
		return false;
	}
	if (Relic->ExclusivityGroup.IsValid())
	{
		for (const FFPSRLOwnedRelic& Owned : OwnedRelics)
		{
			if (Owned.Relic && Owned.Relic != Relic && Owned.Relic->ExclusivityGroup.MatchesTagExact(Relic->ExclusivityGroup))
			{
				return false;	// another relic of the same group is already owned
			}
		}
	}
	FGameplayTagContainer Carried;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		Carried = ASC->GetOwnedGameplayTags();
	}
	if (!Relic->RequiredTags.IsEmpty() && !Carried.HasAny(Relic->RequiredTags))
	{
		return false;
	}
	return !Carried.HasAny(Relic->BlockedTags);
}

bool UFPSRLRelicComponent::GrantRelic(UFPSRLRelicDefinition* Relic)
{
	if (!GetOwner()->HasAuthority() || !GetAbilitySystem() || !CanReceive(Relic))
	{
		return false;
	}
	int32 OwnedIndex = OwnedRelics.IndexOfByPredicate([Relic](const FFPSRLOwnedRelic& Entry) { return Entry.Relic == Relic; });
	if (OwnedIndex == INDEX_NONE)
	{
		OwnedIndex = OwnedRelics.Add({ Relic, 0 });
		Handles.SetNum(OwnedRelics.Num());
	}
	GiveStack(OwnedIndex, OwnedRelics[OwnedIndex].Stacks == 0);
	++OwnedRelics[OwnedIndex].Stacks;

	UE_LOG(LogFPSRL, Log, TEXT("[Relics] %s received %s (%s, x%d)"), *GetNameSafe(GetOwner()), *GetNameSafe(Relic),
		*UEnum::GetValueAsString(Relic->Rarity), OwnedRelics[OwnedIndex].Stacks);
	BroadcastChanged();
	return true;
}

UFPSRLRelicDefinition* UFPSRLRelicComponent::GrantRandomRelic()
{
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const UFPSRLRelicPool* Pool = Settings.RelicPool.LoadSynchronous();
	if (!GetOwner()->HasAuthority() || !Pool)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Relics] No relic pool set in Project Settings > FPSRL Blessings"));
		return nullptr;
	}

	// Eligible relics by rarity, then a rarity roll over the rarities that have any.
	TMap<ERelicRarity, TArray<UFPSRLRelicDefinition*>> ByRarity;
	for (UFPSRLRelicDefinition* Relic : Pool->Relics)
	{
		if (Relic && Relic->SelectionWeight > 0.f && CanReceive(Relic))
		{
			ByRarity.FindOrAdd(Relic->Rarity).Add(Relic);
		}
	}
	float Total = 0.f;
	for (const TPair<ERelicRarity, TArray<UFPSRLRelicDefinition*>>& Entry : ByRarity)
	{
		Total += FMath::Max(0.f, Settings.RelicRarityWeights.FindRef(Entry.Key));
	}
	if (Total <= 0.f)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Relics] %s: no relic can be granted"), *GetNameSafe(GetOwner()));
		return nullptr;
	}

	float Roll = FMath::FRandRange(0.f, Total);
	const TArray<UFPSRLRelicDefinition*>* Rolled = nullptr;
	for (const TPair<ERelicRarity, TArray<UFPSRLRelicDefinition*>>& Entry : ByRarity)
	{
		Roll -= FMath::Max(0.f, Settings.RelicRarityWeights.FindRef(Entry.Key));
		Rolled = &Entry.Value;
		if (Roll <= 0.f)
		{
			break;
		}
	}

	float RelicTotal = 0.f;
	for (const UFPSRLRelicDefinition* Relic : *Rolled)
	{
		RelicTotal += Relic->SelectionWeight;
	}
	float RelicRoll = FMath::FRandRange(0.f, RelicTotal);
	UFPSRLRelicDefinition* Chosen = Rolled->Last();
	for (UFPSRLRelicDefinition* Relic : *Rolled)
	{
		RelicRoll -= Relic->SelectionWeight;
		if (RelicRoll <= 0.f)
		{
			Chosen = Relic;
			break;
		}
	}
	return GrantRelic(Chosen) ? Chosen : nullptr;
}

void UFPSRLRelicComponent::GiveStack(int32 OwnedIndex, bool bFirstStack)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const UFPSRLRelicDefinition* Relic = OwnedRelics[OwnedIndex].Relic;
	if (!ASC || !Relic)
	{
		return;
	}
	FFPSRLGrantSet ToGrant = Relic->Grants;
	if (!bFirstStack)
	{
		ToGrant.Abilities.Reset();
		ToGrant.GrantedTags.Reset();
		ToGrant.GrantCue = FGameplayTag();
	}
	Handles[OwnedIndex].Add(FPSRLGrants::Give(ToGrant, ASC, const_cast<UFPSRLRelicDefinition*>(Relic),
		FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run)));
}

void UFPSRLRelicComponent::ClearRunState()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	for (TArray<FFPSRLGrantHandles>& Stacks : Handles)
	{
		for (FFPSRLGrantHandles& Stack : Stacks)
		{
			FPSRLGrants::Take(Stack, ASC);
		}
	}
	Handles.Reset();
	OwnedRelics.Reset();
	PendingRestore.Reset();
	BroadcastChanged();
}

void UFPSRLRelicComponent::CopyRunStateTo(UFPSRLRelicComponent* Other) const
{
	if (Other)
	{
		Other->PendingRestore = PendingRestore.IsEmpty() ? OwnedRelics : PendingRestore;
	}
}

void UFPSRLRelicComponent::RestoreRunState()
{
	if (!GetOwner()->HasAuthority() || PendingRestore.IsEmpty() || !GetAbilitySystem())
	{
		return;
	}
	OwnedRelics = MoveTemp(PendingRestore);
	PendingRestore.Reset();
	Handles.Reset();
	Handles.SetNum(OwnedRelics.Num());
	for (int32 OwnedIndex = 0; OwnedIndex < OwnedRelics.Num(); ++OwnedIndex)
	{
		for (int32 Stack = 0; Stack < OwnedRelics[OwnedIndex].Stacks; ++Stack)
		{
			GiveStack(OwnedIndex, Stack == 0);
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Relics] %s: restored %d relic(s) after travel"), *GetNameSafe(GetOwner()), OwnedRelics.Num());
	BroadcastChanged();
}
