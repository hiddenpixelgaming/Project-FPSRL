// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLEnemyScalingRules.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLEncounterDefinition.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Data/FPSRLRunDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

namespace FPSRLEnemyScaling
{
	static TAutoConsoleVariable<FString> CVarForceRole(TEXT("fpsrl.Enemy.ForceRole"), TEXT(""),
		TEXT("Test: every enemy a normal combat room mixes is this role (part of its definition's name, e.g. Brute). Empty = the normal mix."));

	FString FEncounterScaling::Describe() const
	{
		return FString::Printf(TEXT("%d player(s), %s profile: health x%.2f (players %.2f, depth %.2f, difficulty %.2f, expedition %.2f, encounter %.2f), damage x%.2f, max enemies %d"),
			Players, bBossProfile ? TEXT("boss") : TEXT("enemy"), GetHealthMultiplier(), PlayerHealth, DepthHealth, DifficultyHealth, ExpeditionHealth, EncounterHealth,
			GetDamageMultiplier(), MaxEnemies);
	}

	int32 CountParticipatingPlayers(const UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		int32 Players = 0;
		if (GameState)
		{
			for (const APlayerState* Player : GameState->PlayerArray)
			{
				Players += Player && !Player->IsOnlyASpectator() && !Player->IsABot() ? 1 : 0;
			}
		}
		return FMath::Max(1, Players);
	}

	FEncounterScaling Compute(const UWorld* World, const UFPSRLEncounterDefinition* Encounter, int32 Players)
	{
		const UFPSRLEnemyScalingSettings& Settings = UFPSRLEnemyScalingSettings::Get();
		FEncounterScaling Scaling;
		Scaling.Players = FMath::Max(1, Players);

		// Player count: the encounter's own profile, else the boss profile for a Final Level Boss, else the enemy one.
		Scaling.bBossProfile = Encounter && Encounter->Kind == EFPSRLEncounterKind::FinalLevelBoss;
		const FFPSRLPlayerCountScaling& Profile = Encounter && Encounter->bOverrideScaling ? Encounter->ScalingOverride
			: Scaling.bBossProfile ? Settings.BossScaling : Settings.EnemyScaling;
		Scaling.PlayerHealth = Profile.GetHealth(Scaling.Players);
		Scaling.PlayerDamage = Profile.GetDamage(Scaling.Players);
		Scaling.MaxEnemies = Profile.GetMaxEnemies(Scaling.Players);

		// Depth and expedition (the run in progress; 1 outside a run).
		const UFPSRLRunSubsystem* Run = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
		if (const UFPSRLDepthDefinition* Depth = Run ? Run->GetCurrentDepth() : nullptr)
		{
			Scaling.DepthHealth = Depth->DepthHealthMultiplier;
			Scaling.DepthDamage = Depth->DepthDamageMultiplier;
		}
		if (Run)
		{
			Scaling.ExpeditionHealth = Run->ExpeditionHealthMultiplier;
			Scaling.ExpeditionDamage = Run->ExpeditionDamageMultiplier;
		}

		// Difficulty, with infinite scaling (+1% health and damage per point by default).
		Scaling.DifficultyHealth = Settings.DifficultyHealthMultiplier * (1.f + Settings.InfiniteScalingPoints * Settings.InfiniteScalingHealthPerPoint);
		Scaling.DifficultyDamage = Settings.DifficultyDamageMultiplier * (1.f + Settings.InfiniteScalingPoints * Settings.InfiniteScalingDamagePerPoint);

		// Encounter-specific (Miniboss / boss placeholders).
		if (Encounter)
		{
			Scaling.EncounterHealth = Encounter->HealthMultiplier;
			Scaling.EncounterDamage = Encounter->DamageMultiplier;
		}
		return Scaling;
	}

	void ApplyToEnemy(APawn* Enemy, const FEncounterScaling& Scaling)
	{
		UFPSRLHealthComponent* Health = Enemy && Enemy->HasAuthority() ? Enemy->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (!Health)
		{
			return;
		}
		const UFPSRLEnemyDefinition* Definition = UFPSRLEnemyScalingSettings::Get().FindDefinition(Enemy->GetClass());
		const float BaseHealth = Definition ? Definition->BaseHealth : Health->DefaultMaxHealth;
		const float BaseDamage = Definition ? Definition->BaseDamageMultiplier : 1.f;
		Health->SetMaxHealthServer(BaseHealth * Scaling.GetHealthMultiplier());
		Health->OutgoingDamageMultiplier = BaseDamage * Scaling.GetDamageMultiplier();
		UE_LOG(LogFPSRL, Verbose, TEXT("[Scaling] %s: base %.0f -> %.0f health, damage x%.2f"), *Enemy->GetName(), BaseHealth, Health->GetMaxHealth(),
			Health->OutgoingDamageMultiplier);
	}

	TArray<TSubclassOf<APawn>> ChooseRoles(const UWorld* World, const TArray<FVector>& SpawnLocations)
	{
		TArray<TSubclassOf<APawn>> Result;
		Result.Init(nullptr, SpawnLocations.Num());
		const UFPSRLRunSubsystem* Run = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
		const int32 Depth = FMath::Max(1, Run ? Run->GetDepthNumber() : 1);
		const FString Forced = CVarForceRole.GetValueOnGameThread();

		struct FCandidate
		{
			const FFPSRLEnemyRoleMix* Role = nullptr;
			const UFPSRLEnemyDefinition* Definition = nullptr;
			TSubclassOf<APawn> Class;
			int32 Count = 0;
		};
		TArray<FCandidate> Candidates;
		for (const FFPSRLEnemyRoleMix& Role : UFPSRLEnemyScalingSettings::Get().RoleMix)
		{
			const UFPSRLEnemyDefinition* Definition = Role.Definition.LoadSynchronous();
			UClass* Class = Definition ? Definition->EnemyClass.LoadSynchronous() : nullptr;
			const bool bWanted = Forced.IsEmpty() ? Role.FirstDepth <= Depth && Role.Weight > 0.f : Definition && Definition->GetName().Contains(Forced);
			if (Class && Class->IsChildOf(APawn::StaticClass()) && bWanted)
			{
				Candidates.Add({ &Role, Definition, Class });
			}
		}
		if (Candidates.IsEmpty())
		{
			return Result;
		}

		// Roles first (weighted, within each role's cap), then places: high-ground roles on the highest spawn points.
		TArray<int32> Picks;
		for (int32 Index = 0; Index < SpawnLocations.Num(); ++Index)
		{
			auto Available = [&Forced](const FCandidate& Candidate)
			{
				return !Forced.IsEmpty() || Candidate.Role->MaxPerEncounter <= 0 || Candidate.Count < Candidate.Role->MaxPerEncounter;
			};
			float Total = 0.f;
			for (const FCandidate& Candidate : Candidates)
			{
				Total += Available(Candidate) ? Candidate.Role->Weight : 0.f;
			}
			if (Total <= 0.f)
			{
				break;	// every role at its cap: the rest are the room's own enemy
			}
			float Roll = FMath::FRand() * Total;
			int32 Pick = INDEX_NONE;
			for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
			{
				if (Available(Candidates[CandidateIndex]))
				{
					Pick = CandidateIndex;
					Roll -= Candidates[CandidateIndex].Role->Weight;
					if (Roll <= 0.f)
					{
						break;
					}
				}
			}
			++Candidates[Pick].Count;
			Picks.Add(Pick);
		}
		Picks.StableSort([&Candidates](int32 A, int32 B) { return Candidates[A].Role->bHighGround && !Candidates[B].Role->bHighGround; });
		TArray<int32> Highest;
		for (int32 Index = 0; Index < SpawnLocations.Num(); ++Index)
		{
			Highest.Add(Index);
		}
		Highest.StableSort([&SpawnLocations](int32 A, int32 B) { return SpawnLocations[A].Z > SpawnLocations[B].Z; });
		TArray<FString> Counts;
		for (int32 Index = 0; Index < Picks.Num(); ++Index)
		{
			Result[Highest[Index]] = Candidates[Picks[Index]].Class;
		}
		for (const FCandidate& Candidate : Candidates)
		{
			if (Candidate.Count > 0)
			{
				Counts.Add(FString::Printf(TEXT("%s x%d"), *Candidate.Definition->DisplayName.ToString(), Candidate.Count));
			}
		}
		UE_LOG(LogFPSRL, Log, TEXT("[Roles] Depth %d: %s"), Depth, *FString::Join(Counts, TEXT(", ")));
		return Result;
	}
}
