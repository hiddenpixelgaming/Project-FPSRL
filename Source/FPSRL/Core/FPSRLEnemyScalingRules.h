// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class APawn;
class UWorld;
class UFPSRLEncounterDefinition;

/**
 * The one place enemy stats are scaled (server). An encounter computes its scaling once, when it starts, from the players
 * taking part at that moment; enemies spawned for it get their final health and damage from it. Players joining or
 * leaving later don't change an encounter that is already running.
 * Layers (each independent, health and damage separate): base stats -> player count -> Depth -> difficulty (+ infinite
 * scaling) -> expedition -> encounter. See UFPSRLEnemyScalingSettings.
 */
namespace FPSRLEnemyScaling
{
	struct FPSRL_API FEncounterScaling
	{
		int32 Players = 1;
		bool bBossProfile = false;
		float PlayerHealth = 1.f, PlayerDamage = 1.f;
		float DepthHealth = 1.f, DepthDamage = 1.f;
		float DifficultyHealth = 1.f, DifficultyDamage = 1.f;
		float ExpeditionHealth = 1.f, ExpeditionDamage = 1.f;
		float EncounterHealth = 1.f, EncounterDamage = 1.f;
		int32 MaxEnemies = MAX_int32;

		float GetHealthMultiplier() const { return PlayerHealth * DepthHealth * DifficultyHealth * ExpeditionHealth * EncounterHealth; }
		float GetDamageMultiplier() const { return PlayerDamage * DepthDamage * DifficultyDamage * ExpeditionDamage * EncounterDamage; }
		FString Describe() const;
	};

	/** Players taking part (every connected player who isn't only spectating). */
	FPSRL_API int32 CountParticipatingPlayers(const UWorld* World);

	/** An encounter's scaling for this many players (Encounter may be null: an ordinary room). */
	FPSRL_API FEncounterScaling Compute(const UWorld* World, const UFPSRLEncounterDefinition* Encounter, int32 Players);

	/** Server: give a freshly spawned enemy its final stats (base from its UFPSRLEnemyDefinition, else its health default). */
	FPSRL_API void ApplyToEnemy(APawn* Enemy, const FEncounterScaling& Scaling);
}
