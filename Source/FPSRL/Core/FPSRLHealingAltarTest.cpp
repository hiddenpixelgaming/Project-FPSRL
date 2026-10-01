// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.HealingAltarTest checks AFPSRLHealingAltar on the host: at full health it refuses and keeps the
// player's use; hurt, using it heals to full; a second use is refused.

#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Rooms/FPSRLHealingAltar.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLHealingAltarTestCommand(TEXT("FPSRL.HealingAltarTest"),
	TEXT("Test: the Healing Altar heals to full once per player ([HealingAltarTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		TSharedRef<TWeakObjectPtr<AFPSRLHealingAltar>> Altar = MakeShared<TWeakObjectPtr<AFPSRLHealingAltar>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Problems, Altar](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!Health || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[HealingAltarTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto Use = [PC, Altar]()
			{
				FText Reason;
				const bool bUsed = Altar->IsValid() && Altar->Get()->TryOffer(PC, Reason);
				return TPair<bool, FString>(bUsed, Reason.ToString());
			};

			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				*Altar = World->SpawnActor<AFPSRLHealingAltar>(Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 150.f, FRotator::ZeroRotator, Params);
				return true;
			}
			switch (*Step)
			{
			case 1004:
			{
				const TPair<bool, FString> Result = Use();
				Check(!Result.Key && Altar->IsValid() && !Altar->Get()->HasBeenUsedBy(PC->PlayerState),
					FString::Printf(TEXT("at full health (%.0f/%.0f): used %d, says '%s', use kept %d (expect refused, use kept)"), Health->GetCurrentHealth(),
						Health->GetMaxHealth(), Result.Key, *Result.Value, Altar->IsValid() && !Altar->Get()->HasBeenUsedBy(PC->PlayerState)));
				Health->ApplyEnvironmentDamage(Health->GetMaxHealth() * 0.6f);
				break;
			}
			case 1008:
				Use();
				break;
			case 1012:
			{
				Check(FMath::IsNearlyEqual(Health->GetCurrentHealth(), Health->GetMaxHealth(), 1.f) && Altar->IsValid() && Altar->Get()->HasBeenUsedBy(PC->PlayerState),
					FString::Printf(TEXT("hurt, then used: %.0f/%.0f (expect full, and the use spent)"), Health->GetCurrentHealth(), Health->GetMaxHealth()));
				Health->ApplyEnvironmentDamage(100.f);
				const TPair<bool, FString> Result = Use();
				Check(!Result.Key, FString::Printf(TEXT("second use: used %d, says '%s' (expect refused)"), Result.Key, *Result.Value));
				// Debug keys: F7 heals to full, F8 kills every enemy (pressed through this player's input).
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("400"), FString());
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F7, IE_Pressed, 1.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F7, IE_Released, 0.f));
				break;
			}
			case 1016:
				Check(FMath::IsNearlyEqual(Health->GetCurrentHealth(), Health->GetMaxHealth(), 1.f),
					FString::Printf(TEXT("F7: %.0f/%.0f (expect full)"), Health->GetCurrentHealth(), Health->GetMaxHealth()));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F8, IE_Pressed, 1.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F8, IE_Released, 0.f));
				break;
			case 1020:
			{
				int32 Living = 0;
				int32 Total = 0;
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					const UFPSRLHealthComponent* EnemyHealth = It->IsPlayerControlled() ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
					Total += EnemyHealth ? 1 : 0;
					Living += EnemyHealth && !EnemyHealth->IsDead() ? 1 : 0;
				}
				Check(Total > 0 && Living == 0, FString::Printf(TEXT("F8: %d of %d enemies still alive (expect 0 of 1+)"), Living, Total));
				UE_LOG(LogFPSRL, Log, TEXT("[HealingAltarTest] done: %d problem(s)"), *Problems);
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
