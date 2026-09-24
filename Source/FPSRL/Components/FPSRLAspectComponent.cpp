// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLAspectComponent.h"
#include "AbilitySystemComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

UFPSRLAspectComponent::UFPSRLAspectComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSRLAspectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFPSRLAspectComponent, ActiveAspect);	// teammates can see each other's aspect
	DOREPLIFETIME_CONDITION(UFPSRLAspectComponent, AspectOptions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFPSRLAspectComponent, AspectEventId, COND_OwnerOnly);
}

void UFPSRLAspectComponent::OnRep_AspectState()
{
	OnAspectStateChanged.Broadcast();
}

void UFPSRLAspectComponent::BroadcastChanged()
{
	GetOwner()->ForceNetUpdate();
	OnAspectStateChanged.Broadcast();
}

AFPSRLPlayerState* UFPSRLAspectComponent::GetOwningPlayerState() const
{
	return Cast<AFPSRLPlayerState>(GetOwner());
}

bool UFPSRLAspectComponent::HasCompletedAspectChoice() const
{
	return ActiveAspect != nullptr || !bAspectsAvailableForWeapon;
}

void UFPSRLAspectComponent::OfferAspects(const FGameplayTag& WeaponTag)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// A new weapon invalidates any aspect picked for the previous one (Lobby only; nothing is applied there).
	if (ActiveAspect && !ActiveAspect->IsCompatibleWithWeapon(WeaponTag))
	{
		ActiveAspect = nullptr;
	}
	if (ActiveAspect)
	{
		AspectOptions.Reset();
		bAspectsAvailableForWeapon = true;
		BroadcastChanged();
		return;
	}

	TArray<UFPSRLAspectDefinition*> Compatible;
	if (const UFPSRLAspectPool* Pool = UFPSRLBoonSettings::Get().AspectPool.LoadSynchronous())
	{
		for (UFPSRLAspectDefinition* Aspect : Pool->Aspects)
		{
			if (Aspect && Aspect->IsCompatibleWithWeapon(WeaponTag))
			{
				Compatible.AddUnique(Aspect);
			}
		}
	}

	// Random subset, no repeats.
	AspectOptions.Reset();
	const int32 Count = UFPSRLBoonSettings::Get().AspectOptionsPerWeapon;
	while (AspectOptions.Num() < Count && !Compatible.IsEmpty())
	{
		const int32 Index = FMath::RandRange(0, Compatible.Num() - 1);
		AspectOptions.Add(Compatible[Index]);
		Compatible.RemoveAtSwap(Index);
	}

	bAspectsAvailableForWeapon = !AspectOptions.IsEmpty();
	++AspectEventId;
	BroadcastChanged();
}

bool UFPSRLAspectComponent::TrySelectAspect(int32 EventId, int32 OptionIndex, const FGameplayTag& WeaponTag)
{
	// Validate: request is for the current offer, nothing chosen yet, option exists and fits the weapon.
	if (!GetOwner()->HasAuthority() || ActiveAspect || EventId != AspectEventId || !AspectOptions.IsValidIndex(OptionIndex))
	{
		return false;
	}
	UFPSRLAspectDefinition* Chosen = AspectOptions[OptionIndex];
	if (!Chosen || !Chosen->IsCompatibleWithWeapon(WeaponTag))
	{
		return false;
	}

	ActiveAspect = Chosen;
	AspectOptions.Reset();
	UE_LOG(LogFPSRL, Log, TEXT("[Aspect] %s chose %s"), *GetNameSafe(GetOwner()), *Chosen->GetName());
	BroadcastChanged();
	return true;
}

void UFPSRLAspectComponent::ApplyActiveAspect()
{
	AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS || !PS->HasAuthority() || !ActiveAspect || bApplied)
	{
		return;
	}

	FFPSRLGrantSet ToGrant = ActiveAspect->Grants;
	if (ActiveAspect->AspectTag.IsValid())
	{
		ToGrant.GrantedTags.AddTag(ActiveAspect->AspectTag);
	}
	AppliedHandles = FPSRLGrants::Give(ToGrant, PS->GetAbilitySystemComponent(), ActiveAspect,
		FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run));
	bApplied = true;
	UE_LOG(LogFPSRL, Log, TEXT("[Aspect] %s active: %s"), *GetNameSafe(PS), *ActiveAspect->GetName());
}

void UFPSRLAspectComponent::ClearRunState()
{
	AFPSRLPlayerState* PS = GetOwningPlayerState();
	if (!PS || !PS->HasAuthority())
	{
		return;
	}

	FPSRLGrants::Take(AppliedHandles, PS->GetAbilitySystemComponent());
	bApplied = false;
	ActiveAspect = nullptr;
	AspectOptions.Reset();
	BroadcastChanged();
}

void UFPSRLAspectComponent::CopyChoiceTo(UFPSRLAspectComponent* Other) const
{
	if (Other)
	{
		Other->ActiveAspect = ActiveAspect;
		Other->bAspectsAvailableForWeapon = bAspectsAvailableForWeapon;
	}
}
