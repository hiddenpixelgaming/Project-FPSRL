// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FPSRLTypes.h"

class AActor;
class AController;
class APawn;
class AFPSRLPlayerState;
class UAbilitySystemComponent;

/**
 * How a player's attack turns into damage and Blessing events (server). Called by UFPSRLHealthComponent for every hit.
 *
 *  - Source: the attacker's own body = Melee; anything else they fired (projectile, explosion) = Ranged.
 *  - Damage x DamageMultiplier x <Source>DamageMultiplier; a critical (CritChance + <Source>CritChance) x CritDamageMultiplier;
 *    then the attacker's Blessings may adjust it (UFPSRLBoonComponent::ModifyOutgoingDamage, e.g. "+50% vs burning").
 *  - After it lands: Event.Hit (and Event.Kill if it killed) to the attacker's ability system, with the source tag in
 *    InstigatorTags and Hit.Critical in TargetTags. Blessings react to these (UFPSRLBoonComponent) and GAS abilities can
 *    trigger on them too.
 * Enemies' attacks are untouched.
 */
namespace FPSRLCombat
{
	struct FPlayerHit
	{
		UAbilitySystemComponent* AttackerASC = nullptr;
		APawn* AttackerPawn = nullptr;
		AFPSRLPlayerState* AttackerState = nullptr;
		EFPSRLItemSource Source = EFPSRLItemSource::Ranged;
		bool bCritical = false;
	};

	/** Scales InOutDamage for a player's attack. Returns an empty hit (no ASC) for anyone else's. */
	FPSRL_API FPlayerHit ResolvePlayerHit(AController* InstigatedBy, AActor* DamageCauser, AActor* Target, float& InOutDamage);

	/** Event.Hit (+ Event.Kill) to the attacker's ability system. */
	FPSRL_API void SendHitEvents(const FPlayerHit& Hit, AActor* Target, float Damage, bool bKilled);

	FPSRL_API FGameplayTag GetSourceTag(EFPSRLItemSource Source);
}
