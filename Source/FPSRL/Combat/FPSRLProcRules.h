// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

struct FFPSRLBlessingTrigger;
struct FRandomStream;

/**
 * The one proc evaluation every Blessing uses (server). Weapons, melee and abilities only report events (Event.Attack,
 * Event.Hit, Event.Kill with an attack id); the Blessing's FFPSRLBlessingTrigger decides what they mean. See
 * UFPSRLBoonComponent::HandleCombatEvent for where events come in and FFPSRLBlessingTrigger for the models.
 */
namespace FPSRLProcs
{
	/** Per player, per owned Blessing trigger (never shared between players). */
	struct FPSRL_API FProcState
	{
		float Progress = 0.f;			// Accumulate: counter toward Threshold
		double LastProcTime = -1.0e9;	// internal cooldown
		int32 LastAttackId = INDEX_NONE;	// OncePerAttack: the attack already evaluated
		int32 Evaluations = 0;			// eligible events that were evaluated
		int32 ProcCount = 0;
	};

	/** One eligible event, as the evaluation sees it. */
	struct FPSRL_API FProcEvent
	{
		double Now = 0.0;
		int32 AttackId = INDEX_NONE;	// INDEX_NONE = not part of a tracked attack
		float Damage = 0.f;
		bool bCritical = false;
		float EventInterval = 0.f;		// seconds between this source's events right now (Normalized); 0 = unknown
		int32 UpgradeLevel = 0;
		FRandomStream* Random = nullptr;	// tests: a seeded stream; null = FMath::FRand (server)
	};

	/** The chance an event rolls against (1 for Accumulate / Periodic, which don't roll). */
	FPSRL_API float GetChance(const FFPSRLBlessingTrigger& Trigger, const FProcEvent& Event);

	/** Evaluate one event: scope, conditions (critical, damage), cooldown, model. Updates State; true = proc. Target tag
	 *  requirements are checked by the caller (they need the world). */
	FPSRL_API bool Evaluate(const FFPSRLBlessingTrigger& Trigger, FProcState& State, const FProcEvent& Event);
}
