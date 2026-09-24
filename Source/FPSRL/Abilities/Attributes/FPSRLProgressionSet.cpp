// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Attributes/FPSRLProgressionSet.h"
#include "Net/UnrealNetwork.h"

UFPSRLProgressionSet::UFPSRLProgressionSet()
	: MaxBoonSlots(20.f)
{
}

void UFPSRLProgressionSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRLProgressionSet, MaxBoonSlots, COND_None, REPNOTIFY_Always);
}

void UFPSRLProgressionSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetMaxBoonSlotsAttribute())
	{
		NewValue = FMath::Max(0.f, FMath::RoundToFloat(NewValue));
	}
}

void UFPSRLProgressionSet::OnRep_MaxBoonSlots(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRLProgressionSet, MaxBoonSlots, OldValue);
}
