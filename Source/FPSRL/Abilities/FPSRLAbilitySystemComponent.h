// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "FPSRLAbilitySystemComponent.generated.h"

/**
 * Project ASC. Used for players (owned by AFPSRLPlayerState) and for enemies (added as a component on the enemy pawn).
 *
 * Differences from the engine default:
 *  - Replicates by default, so a Blueprint-added instance on an enemy works without ticking "Component Replicates".
 *  - Defaults to Minimal replication mode (right for AI: only tags/cues go to clients).
 *    AFPSRLPlayerState switches its instance to Mixed so the owning player also gets full effect data.
 */
UCLASS(ClassGroup = (Abilities), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UFPSRLAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
