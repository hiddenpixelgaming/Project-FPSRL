// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/FPSRLAbilitySystemComponent.h"

UFPSRLAbilitySystemComponent::UFPSRLAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}
