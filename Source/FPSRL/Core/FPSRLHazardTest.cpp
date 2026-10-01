// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.HazardTest checks AFPSRLHazardZone on the host: a player and an enemy standing on the hazard surface
// both take its damage (DamagePerSecond, every DamageInterval); standing 40 cm higher (on the stone beside a pool) they
// take none; a god-mode player takes none.

#include "Components/FPSRLHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Rooms/FPSRLHazardZone.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLHazardTestCommand(TEXT("FPSRL.HazardTest"),
	TEXT("Test: hazard zones hurt only those standing in them ([HazardTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		TSharedRef<TWeakObjectPtr<AFPSRLHazardZone>> Hazard = MakeShared<TWeakObjectPtr<AFPSRLHazardZone>>();
		TSharedRef<TWeakObjectPtr<UFPSRLHealthComponent>> EnemyHealth = MakeShared<TWeakObjectPtr<UFPSRLHealthComponent>>();
		TSharedRef<FVector2f> Before = MakeShared<FVector2f>();	// player, enemy health at the last checkpoint
		TSharedRef<float> SurfaceZ = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Problems, Hazard, EnemyHealth, Before, SurfaceZ](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			ACharacter* Pawn = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			UFPSRLHealthComponent* PlayerHealth = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!PlayerHealth || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[HazardTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto Losses = [PlayerHealth, EnemyHealth, Before]()
			{
				const float Enemy = EnemyHealth->IsValid() ? EnemyHealth->Get()->GetCurrentHealth() : 0.f;
				const FVector2f Lost(Before->X - PlayerHealth->GetCurrentHealth(), Before->Y - Enemy);
				*Before = FVector2f(PlayerHealth->GetCurrentHealth(), Enemy);
				return Lost;
			};
			auto PlaceHazard = [Hazard, SurfaceZ](float Z)
			{
				if (AFPSRLHazardZone* Zone = Hazard->Get())
				{
					FVector At = Zone->GetActorLocation();
					At.Z = Z - 2.f;
					Zone->SetActorLocation(At);
				}
			};

			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());	// stationary, no AI
				return true;
			}
			switch (*Step)
			{
			case 1004:
			{
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					if (*It != Pawn && !It->IsPlayerControlled())
					{
						*EnemyHealth = It->FindComponentByClass<UFPSRLHealthComponent>();
					}
				}
				// The hazard surface at the player's feet, under both of them (same as the arena kit's hazard_zone).
				*SurfaceZ = Pawn->GetActorLocation().Z - Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const FVector Center = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 75.f;
				AFPSRLHazardZone* Zone = World->SpawnActor<AFPSRLHazardZone>(FVector(Center.X, Center.Y, *SurfaceZ - 2.f), FRotator::ZeroRotator);
				Zone->GetZone()->SetBoxExtent(FVector(300.f, 300.f, 27.f));
				*Hazard = Zone;
				Losses();
				Check(EnemyHealth->IsValid(), FString::Printf(TEXT("hazard placed under the player and an enemy (%.0f dps every %.1f s)"), Zone->DamagePerSecond, Zone->DamageInterval));
				break;
			}
			case 1012:
			{
				const FVector2f Lost = Losses();
				const float Expected = Hazard->IsValid() ? Hazard->Get()->DamagePerSecond * 2.f : 0.f;
				Check(Lost.X >= Expected * 0.7f && Lost.X <= Expected * 1.3f, FString::Printf(TEXT("player standing in it for 2 s lost %.0f (expect about %.0f)"), Lost.X, Expected));
				Check(Lost.Y >= Expected * 0.7f && Lost.Y <= Expected * 1.3f, FString::Printf(TEXT("enemy standing in it for 2 s lost %.0f (expect about %.0f)"), Lost.Y, Expected));
				PlaceHazard(*SurfaceZ - 40.f);	// now they stand 40 cm above it, like on the stone beside a pool
				break;
			}
			case 1020:
			{
				const FVector2f Lost = Losses();
				Check(Lost.X == 0.f && Lost.Y == 0.f, FString::Printf(TEXT("40 cm above the surface for 2 s: player lost %.0f, enemy lost %.0f (expect 0)"), Lost.X, Lost.Y));
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				PlaceHazard(*SurfaceZ);
				break;
			}
			case 1028:
			{
				const FVector2f Lost = Losses();
				Check(Lost.X == 0.f && Lost.Y > 0.f, FString::Printf(TEXT("god mode player in it for 2 s lost %.0f (expect 0), enemy lost %.0f (expect > 0)"), Lost.X, Lost.Y));
				Check(Hazard->IsValid() && Hazard->Get()->GetTicksApplied() > 0, FString::Printf(TEXT("damage ticks applied: %d"), Hazard->IsValid() ? Hazard->Get()->GetTicksApplied() : 0));
				UE_LOG(LogFPSRL, Log, TEXT("[HazardTest] done: %d problem(s)"), *Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			default:
				break;
			}
			return true;
		}), 0.25f);
	}));
#endif
