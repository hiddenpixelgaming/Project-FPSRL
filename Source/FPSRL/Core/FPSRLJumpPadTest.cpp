// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.JumpPadTest (run in an arena level opened directly with the match game mode) puts the player on
// every AFPSRLJumpPad in turn and logs where the launch lands (position, floor height, walking again); then puts an
// enemy on the first pad and checks it is NOT launched (pads are for players only).

#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Rooms/FPSRLJumpPad.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLJumpPadTestCommand(TEXT("FPSRL.JumpPadTest"),
	TEXT("Test: every jump pad launches the player to its landing; enemies are not launched ([JumpPadTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		TSharedRef<TArray<TWeakObjectPtr<AFPSRLJumpPad>>> Pads = MakeShared<TArray<TWeakObjectPtr<AFPSRLJumpPad>>>();
		TSharedRef<TWeakObjectPtr<ACharacter>> Enemy = MakeShared<TWeakObjectPtr<ACharacter>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Problems, Pads, Enemy](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			ACharacter* Pawn = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (!Pawn || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[JumpPadTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto PutOnPad = [](ACharacter* Who, const AFPSRLJumpPad* Pad)
			{
				Who->GetCharacterMovement()->StopMovementImmediately();
				Who->SetActorLocation(Pad->GetActorLocation() + FVector(0.f, 0.f, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.f),
					false, nullptr, ETeleportType::TeleportPhysics);
			};

			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				for (TActorIterator<AFPSRLJumpPad> It(World); It; ++It)
				{
					Pads->Add(*It);
				}
				Pads->Sort([](const TWeakObjectPtr<AFPSRLJumpPad>& A, const TWeakObjectPtr<AFPSRLJumpPad>& B) { return A->GetName() < B->GetName(); });
				Check(Pads->Num() > 0, FString::Printf(TEXT("%d jump pad(s) in the level"), Pads->Num()));
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				return Pads->Num() > 0;
			}
			// Each pad: put the player on it, 4 s later report the landing.
			const int32 PadIndex = (*Step - 1001) / 16;
			const int32 Phase = (*Step - 1001) % 16;
			if (PadIndex < Pads->Num())
			{
				const AFPSRLJumpPad* Pad = (*Pads)[PadIndex].Get();
				if (Phase == 0)
				{
					PutOnPad(Pawn, Pad);
				}
				else if (Phase == 15)
				{
					const FVector Land = Pawn->GetActorLocation();
					const float Feet = Land.Z - Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
					const bool bWalking = Pawn->GetCharacterMovement()->IsMovingOnGround();
					Check(bWalking && FVector::Dist2D(Land, Pad->GetActorLocation()) > 200.f,
						FString::Printf(TEXT("%s velocity %s: landed at %s, feet at %.0f, %s"), *Pad->GetActorLabel(), *Pad->Velocity.ToCompactString(),
							*FVector(Land.X, Land.Y, 0.f).ToCompactString(), Feet, bWalking ? TEXT("walking") : TEXT("still in the air")));
				}
				return true;
			}
			const int32 EnemyStep = *Step - 1001 - Pads->Num() * 16;
			if (EnemyStep == 0)
			{
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());	// stationary, no AI
			}
			else if (EnemyStep == 4)
			{
				for (TActorIterator<ACharacter> It(World); It; ++It)
				{
					if (*It != Pawn && !It->IsPlayerControlled())
					{
						*Enemy = *It;
					}
				}
				if (Enemy->IsValid() && (*Pads)[0].IsValid())
				{
					PutOnPad(Enemy->Get(), (*Pads)[0].Get());
				}
			}
			else if (EnemyStep == 12)
			{
				const AFPSRLJumpPad* Pad = (*Pads)[0].Get();
				const float Moved = Enemy->IsValid() && Pad ? FVector::Dist2D(Enemy->Get()->GetActorLocation(), Pad->GetActorLocation()) : -1.f;
				Check(Moved >= 0.f && Moved < 100.f, FString::Printf(TEXT("enemy put on %s stayed there (%.0f cm from it)"), Pad ? *Pad->GetActorLabel() : TEXT("?"), Moved));
				UE_LOG(LogFPSRL, Log, TEXT("[JumpPadTest] done: %d problem(s)"), *Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.25f);
	}));
#endif
