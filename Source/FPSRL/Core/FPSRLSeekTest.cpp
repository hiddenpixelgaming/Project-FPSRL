// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.SeekTest checks that a shooter enemy (its real behavior profile) that loses sight of the player comes
// looking (to where it last saw the player, then toward them) instead of standing still, and the F9 fast-move key
// (x5 walk speed, restored when turned off) ([SeekTest]).

#include "AI/FPSRLEnemyAIController.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLSeekTestCommand(TEXT("FPSRL.SeekTest"),
	TEXT("Test: an enemy that loses sight of the player comes looking; F9 fast move ([SeekTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		struct FState
		{
			int32 Step = 0;
			int32 Problems = 0;
			TWeakObjectPtr<AFPSRLEnemyAIController> AI;
			FVector WallStart = FVector::ZeroVector;
			int32 IdleChecks = 0;
			float BaseSpeed = 0.f;
		};
		TSharedRef<FState> Live = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Live](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			ACharacter* Pawn = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (!Pawn || !World->GetGameState())
			{
				return ++Live->Step < 400;
			}
			auto Check = [Live](bool bOk, const FString& What)
			{
				Live->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[SeekTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			++Live->Step;
			if (Live->Step < 1000)
			{
				Live->Step = 1000;
				// Run it in an arena opened directly (no player start): stand in its entrance vestibule, facing in.
				Pawn->TeleportTo(FVector(300.f, 0.f, 120.f), FRotator::ZeroRotator);
				PC->SetControlRotation(FRotator::ZeroRotator);
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				return true;
			}
			if (Live->Step == 1002)
			{
				PC->ServerTestCommand(TEXT("SpawnTestAI"), TEXT("1500"), TEXT("0"));	// ahead, in the arena (the Lobby has no navmesh)
				return true;
			}
			AFPSRLEnemyAIController* AI = Live->AI.Get();
			switch (Live->Step)
			{
			case 1004:
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					if (AFPSRLEnemyAIController* Found = Cast<AFPSRLEnemyAIController>(It->GetController()))
					{
						Live->AI = Found;
					}
				}
				if (Live->AI.IsValid())
				{
					Live->AI->SetEncounterActive(true);
				}
				break;
			case 1012:
				if (AI)
				{
					// A wide wall between them: it loses sight.
					const FVector From = AI->GetPawn()->GetActorLocation();
					const FVector Middle = (From + Pawn->GetActorLocation()) * 0.5f;
					AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(Middle, (Pawn->GetActorLocation() - Middle).Rotation());
					Wall->SetMobility(EComponentMobility::Movable);
					Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					Wall->SetActorScale3D(FVector(0.3f, 8.f, 6.f));
					Live->WallStart = From;
				}
				break;
			default:
				if (Live->Step > 1012 && Live->Step <= 1040 && AI)
				{
					Live->IdleChecks += AI->GetAIState() == EFPSRLEnemyAIState::Idle ? 1 : 0;	// 7 s behind the wall
				}
				break;
			case 1041:
			{
				const float Moved = AI ? FVector::Dist2D(AI->GetPawn()->GetActorLocation(), Live->WallStart) : 0.f;
				Check(AI && Moved > 300.f && Live->IdleChecks == 0,
					FString::Printf(TEXT("out of sight behind a wall for 7 s: moved %.0f cm, idle %d of 28 checks, now %s (expect it comes looking: moves, never idle)"),
						Moved, Live->IdleChecks, AI ? *AI->Describe() : TEXT("no AI")));
				// F9 fast move.
				Live->BaseSpeed = Pawn->GetCharacterMovement()->MaxWalkSpeed;
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F9, IE_Pressed, 1.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F9, IE_Released, 0.f));
				break;
			}
			case 1043:
				Check(FMath::IsNearlyEqual(Pawn->GetCharacterMovement()->MaxWalkSpeed, Live->BaseSpeed * 5.f, 1.f),
					FString::Printf(TEXT("F9 on: walk speed %.0f (expect %.0f)"), Pawn->GetCharacterMovement()->MaxWalkSpeed, Live->BaseSpeed * 5.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F9, IE_Pressed, 1.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F9, IE_Released, 0.f));
				break;
			case 1045:
				Check(FMath::IsNearlyEqual(Pawn->GetCharacterMovement()->MaxWalkSpeed, Live->BaseSpeed, 1.f),
					FString::Printf(TEXT("F9 off: walk speed %.0f (expect %.0f)"), Pawn->GetCharacterMovement()->MaxWalkSpeed, Live->BaseSpeed));
				UE_LOG(LogFPSRL, Log, TEXT("[SeekTest] done: %d problem(s)"), Live->Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.25f);
	}));
#endif
