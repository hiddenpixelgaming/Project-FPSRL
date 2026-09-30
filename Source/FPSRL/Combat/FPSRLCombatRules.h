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
		/** The attack this hit belongs to (every pellet of a shot / target of a swing shares it); INDEX_NONE = untracked. */
		int32 AttackId = INDEX_NONE;
	};

	/** Scales InOutDamage for a player's attack. Returns an empty hit (no ASC) for anyone else's. */
	FPSRL_API FPlayerHit ResolvePlayerHit(AController* InstigatedBy, AActor* DamageCauser, AActor* Target, float& InOutDamage);

	/** Event.Hit (+ Event.Kill) to the attacker's ability system. */
	FPSRL_API void SendHitEvents(const FPlayerHit& Hit, AActor* Target, float Damage, bool bKilled);

	/** Server: a player made an attack (hit or miss). Event.Attack to their ability system, once per AttackId (calls for
	 *  the same attack, e.g. each pellet of a shotgun blast, are ignored). */
	FPSRL_API void NotifyAttack(APawn* Attacker, EFPSRLItemSource Source, int32 AttackId);

	/** A fresh attack id (unique on this machine; an attacker's attacks all come from one machine). */
	FPSRL_API int32 NewAttackId();

	FPSRL_API FGameplayTag GetSourceTag(EFPSRLItemSource Source);
}
