// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLProcRules.h"
#include "Data/FPSRLBlessingEffects.h"
#include "Math/RandomStream.h"

namespace FPSRLProcs
{
	float GetChance(const FFPSRLBlessingTrigger& Trigger, const FProcEvent& Event)
	{
		switch (Trigger.Model)
		{
		case EFPSRLProcModel::ChancePerEvent:
			return FMath::Clamp(Trigger.Chance + Trigger.ChancePerUpgrade * Event.UpgradeLevel, 0.f, 1.f);
		case EFPSRLProcModel::Normalized:
		{
			// Chance per event = target procs per second x seconds between events (the source's current rate).
			const float Interval = Event.EventInterval > 0.f ? Event.EventInterval : Trigger.FallbackInterval;
			const float Rate = Trigger.ProcsPerSecond + Trigger.ProcsPerSecondPerUpgrade * Event.UpgradeLevel;
			return FMath::Clamp(Rate * Interval, Trigger.MinChance, FMath::Max(Trigger.MinChance, Trigger.MaxChance));
		}
		default:
			return 1.f;
		}
	}

	bool Evaluate(const FFPSRLBlessingTrigger& Trigger, FProcState& State, const FProcEvent& Event)
	{
		// Scope: once per attack ignores the attack's other pellets / targets.
		if (Trigger.Scope == EFPSRLProcScope::OncePerAttack && Event.AttackId != INDEX_NONE)
		{
			if (Event.AttackId == State.LastAttackId)
			{
				return false;
			}
			State.LastAttackId = Event.AttackId;
		}
		if ((Trigger.bCriticalOnly && !Event.bCritical) || Event.Damage < Trigger.MinDamage)
		{
			return false;
		}
		++State.Evaluations;
		if (Trigger.InternalCooldown > 0.f && Event.Now - State.LastProcTime < Trigger.InternalCooldown)
		{
			return false;	// blocked (not queued)
		}

		bool bProc = false;
		switch (Trigger.Model)
		{
		case EFPSRLProcModel::Accumulate:
			State.Progress += Trigger.AccumulateBy == EFPSRLProcAccumulation::Damage ? Event.Damage : 1.f;
			if (State.Progress >= Trigger.Threshold)
			{
				State.Progress = 0.f;
				bProc = true;
			}
			break;
		case EFPSRLProcModel::Periodic:
			bProc = true;	// the timer is the event
			break;
		default:
		{
			const float Chance = GetChance(Trigger, Event);
			const float Roll = Event.Random ? Event.Random->FRand() : FMath::FRand();
			bProc = Chance >= 1.f || Roll < Chance;
			break;
		}
		}
		if (bProc)
		{
			State.LastProcTime = Event.Now;
			++State.ProcCount;
		}
		return bProc;
	}
}
