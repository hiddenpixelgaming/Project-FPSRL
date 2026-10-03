// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.SquadTest [seconds] (host, during a real run, e.g. Lobby + FPSRLAutoRun with
// fpsrl.Autopilot.FightSeconds 30). Once the first encounter
// forms squads, every second: each squad's leader, how far each member is from it and how far the leader moved
// ([SquadTest]); a screenshot after a few seconds (rendered runs); a summary at the end. Grunts must keep together
// (members within a few metres of the leader) while the leader moves, and still shoot.

#include "AI/FPSRLEnemyAIController.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLSquadTest
{
	struct FState
	{
		double StartTime = 0.0;
		double NextSample = 0.0;
		float Duration = 30.f;
		bool bStarted = false;
		bool bShot = false;
		TMap<TWeakObjectPtr<AFPSRLEnemyAIController>, FVector> LastLeaderSpot;
		int32 Samples = 0;
		float SumDistance = 0.f;
		int32 DistanceCount = 0;
		float MaxDistance = 0.f;
		float LeaderTravel = 0.f;
		int32 MaxSquads = 0;
		int32 MaxSquadSize = 0;
	};
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLSquadTestCommand(TEXT("FPSRL.SquadTest"),
	TEXT("Test (host, an arena): start the room's encounter and log how the Grunt squads keep together ([SquadTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLSquadTest::FState> S = MakeShared<FPSRLSquadTest::FState>();
		S->Duration = Args.IsEmpty() ? 30.f : FCString::Atof(*Args[0]);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			if (!PC || !PC->GetPawn())
			{
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (!S->bStarted)
			{
				// Watches the first encounter that forms squads (a single arena has no navmesh, so this runs in a
				// real run: FPSRLAutoRun with fpsrl.Autopilot.FightSeconds).
				bool bAnySquad = false;
				for (TActorIterator<AFPSRLEnemyAIController> It(World); It && !bAnySquad; ++It)
				{
					bAnySquad = It->GetPawn() && It->GetSquadSize() > 1;
				}
				if (!bAnySquad)
				{
					return true;
				}
				S->bStarted = true;
				S->StartTime = Now;
				S->NextSample = Now + 1.0;
				UE_LOG(LogFPSRL, Log, TEXT("[SquadTest] squads formed, watching"));
				return true;
			}
			if (!S->bShot && Now - S->StartTime > 5.0)
			{
				S->bShot = true;
				PC->ConsoleCommand(TEXT("Shot"));
			}
			if (Now < S->NextSample)
			{
				return true;
			}
			S->NextSample = Now + 1.0;
			++S->Samples;
			// Each squad once, through its leader.
			TSet<AFPSRLEnemyAIController*> Leaders;
			int32 Shots = 0;
			for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
			{
				Shots += It->GetAttacksExecuted();
				if (It->GetPawn() && It->GetSquadSize() > 1)
				{
					Leaders.Add(It->GetSquadLeader());
				}
			}
			S->MaxSquads = FMath::Max(S->MaxSquads, Leaders.Num());
			for (AFPSRLEnemyAIController* Leader : Leaders)
			{
				const FVector LeaderSpot = Leader->GetPawn()->GetActorLocation();
				float Moved = 0.f;
				if (const FVector* Last = S->LastLeaderSpot.Find(Leader))
				{
					Moved = FVector::Dist2D(*Last, LeaderSpot);
					S->LeaderTravel += Moved;
				}
				S->LastLeaderSpot.Add(Leader, LeaderSpot);
				TArray<FString> Distances;
				for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
				{
					if (*It != Leader && It->GetPawn() && It->GetSquadLeader() == Leader)
					{
						const float Distance = FVector::Dist2D(It->GetPawn()->GetActorLocation(), LeaderSpot) / 100.f;
						Distances.Add(FString::Printf(TEXT("%.1f"), Distance));
						S->SumDistance += Distance;
						S->MaxDistance = FMath::Max(S->MaxDistance, Distance);
						++S->DistanceCount;
					}
				}
				S->MaxSquadSize = FMath::Max(S->MaxSquadSize, Leader->GetSquadSize());
				UE_LOG(LogFPSRL, Log, TEXT("[SquadTest] t=%2.0f squad of %d led by %s (%s): members at [%s] m, leader moved %.1f m, shots so far %d"),
					Now - S->StartTime, Leader->GetSquadSize(), *Leader->GetPawn()->GetName(), *UEnum::GetValueAsString(Leader->GetAIState()),
					*FString::Join(Distances, TEXT(", ")), Moved / 100.f, Shots);
			}
			if (Now - S->StartTime >= S->Duration)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[SquadTest] done: %d squad(s), up to %d members; member-to-leader distance avg %.1f m, max %.1f m; leaders moved %.1f m in total; shots %d"),
					S->MaxSquads, S->MaxSquadSize, S->DistanceCount > 0 ? S->SumDistance / S->DistanceCount : 0.f, S->MaxDistance, S->LeaderTravel / 100.f, Shots);
				return false;
			}
			return true;
		}));
	}));
#endif
