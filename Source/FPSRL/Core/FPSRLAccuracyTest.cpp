// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.AccuracyTest <cm> measures how often one shooter enemy (its real behavior profile) hits the player at
// that distance: 8 s with the player standing still, then 8 s strafing side to side. Fair enemies hit a still player
// often and a moving one rarely ([AccuracyTest]).

#include "AI/FPSRLEnemyAIController.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFPSRLAccuracyTestCommand(TEXT("FPSRL.AccuracyTest"),
	TEXT("Test: hits taken from one shooter enemy, standing still vs strafing ([AccuracyTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		const FString Distance = Args.IsEmpty() ? FString(TEXT("1200")) : Args[0];
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		struct FState
		{
			int32 Phase = 0;	// 0 setup, 1 standing, 2 strafing, 3 done
			double PhaseStart = 0.0;
			int32 Hits[3] = { 0, 0, 0 };
			float Lost[3] = { 0.f, 0.f, 0.f };
			int32 Shots[3] = { 0, 0, 0 };
			TWeakObjectPtr<AFPSRLEnemyAIController> AI;
			FDelegateHandle DamagedHandle;
			bool bSpawned = false;
		};
		TSharedRef<FState> Live = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Live, Distance](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!Health || !World->GetGameState())
			{
				return true;
			}
			const double Now = World->GetTimeSeconds();
			if (!Live->bSpawned)
			{
				Live->bSpawned = true;
				PC->ServerTestCommand(TEXT("SpawnTestAI"), FString::Printf(TEXT("-%s"), *Distance), TEXT("0"));	// behind: a clear line
				TWeakPtr<FState> Weak = Live;
				Live->DamagedHandle = Health->OnDamagedBy.AddLambda([Weak](float Damage, APawn*)
				{
					if (TSharedPtr<FState> State = Weak.Pin(); State && State->Phase >= 1 && State->Phase <= 2)
					{
						++State->Hits[State->Phase];
						State->Lost[State->Phase] += Damage;
					}
				});
				Live->PhaseStart = Now;
				return true;
			}
			if (Live->Phase == 0 && Now - Live->PhaseStart > 1.0)
			{
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					if (AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(It->GetController()))
					{
						Live->AI = AI;
					}
				}
				if (!Live->AI.IsValid())
				{
					return true;
				}
				if (UFPSRLHealthComponent* EnemyHealth = Live->AI->GetPawn()->FindComponentByClass<UFPSRLHealthComponent>())
				{
					EnemyHealth->OutgoingDamageMultiplier = 0.2f;	// as in a room (the shooter definition's damage scaling): 15 per bullet
				}
				Live->AI->SetEncounterActive(true);
				Live->Phase = 1;
				Live->PhaseStart = Now;
				Live->Shots[1] = Live->AI->GetAttacksExecuted();
				return true;
			}
			if (Live->Phase == 2)
			{
				// Strafe: side to side, turning every second.
				const float Side = FMath::Fmod(Now - Live->PhaseStart, 2.0) < 1.0 ? 1.f : -1.f;
				Pawn->AddMovementInput(Pawn->GetActorRightVector(), Side);
			}
			if ((Live->Phase == 1 || Live->Phase == 2) && Now - Live->PhaseStart > 8.0)
			{
				Live->Shots[Live->Phase] = Live->AI.IsValid() ? Live->AI->GetAttacksExecuted() - Live->Shots[Live->Phase] : 0;
				UE_LOG(LogFPSRL, Log, TEXT("[AccuracyTest] %s cm, %s for 8 s: %d hits, %.0f damage, %d bursts fired"), *Distance,
					Live->Phase == 1 ? TEXT("standing still") : TEXT("strafing"), Live->Hits[Live->Phase], Live->Lost[Live->Phase], Live->Shots[Live->Phase]);
				Health->Heal(Health->GetMaxHealth());
				if (Live->Phase == 1)
				{
					Live->Phase = 2;
					Live->PhaseStart = Now;
					Live->Shots[2] = Live->AI.IsValid() ? Live->AI->GetAttacksExecuted() : 0;
					return true;
				}
				Health->OnDamagedBy.Remove(Live->DamagedHandle);
				UE_LOG(LogFPSRL, Log, TEXT("[AccuracyTest] done"));
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.f);
	}));
#endif
