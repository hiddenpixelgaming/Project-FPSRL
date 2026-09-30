// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.ScalingTest checks enemy scaling headless on the host: the same enemy scaled for 1-4 players as a normal
// enemy, an Elite and a Final Level Boss (final health and encounter cap), infinite scaling on health and on the damage an
// enemy deals, and the live player count.

#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLEnemyScalingRules.h"
#include "Core/FPSRLPlayerController.h"
#include "Data/FPSRLEncounterDefinition.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLScalingTest
{
	struct FCase
	{
		FString Label;
		int32 Players = 1;
		float ExpectedHealth = 0.f;
		int32 ExpectedMax = 0;
		int32 ActualMax = 0;
		TWeakObjectPtr<APawn> Enemy;
	};

	APawn* SpawnEnemy(UWorld* World, const FVector& At)
	{
		UClass* EnemyClass = LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
		const FTransform Transform(At);
		APawn* Enemy = EnemyClass ? World->SpawnActorDeferred<APawn>(EnemyClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Enemy)
		{
			Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
			Enemy->FinishSpawning(Transform);
		}
		return Enemy;
	}
}

static FAutoConsoleCommandWithWorld GFPSRLScalingTestCommand(TEXT("FPSRL.ScalingTest"),
	TEXT("Host test: enemy health / damage / cap for 1-4 players, Elite and Final Level Boss, infinite scaling ([ScalingTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<TArray<FPSRLScalingTest::FCase>> Cases = MakeShared<TArray<FPSRLScalingTest::FCase>>();
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		TSharedRef<float> PlayerHealthBefore = MakeShared<float>(0.f);
		TSharedRef<TWeakObjectPtr<APawn>> DamageEnemy = MakeShared<TWeakObjectPtr<APawn>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Cases, Problems, PlayerHealthBefore, DamageEnemy](float)
		{
			using namespace FPSRLScalingTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* PlayerHealth = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!PlayerHealth || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[ScalingTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};

			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				const UFPSRLEnemyScalingSettings& Settings = UFPSRLEnemyScalingSettings::Get();
				const UFPSRLEnemyDefinition* Definition = Settings.FindDefinition(LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C")));
				Check(Definition != nullptr, FString::Printf(TEXT("normal shooter has an enemy definition (base health %.0f)"), Definition ? Definition->BaseHealth : 0.f));
				const float Base = Definition ? Definition->BaseHealth : 100.f;
				const int32 LivePlayers = FPSRLEnemyScaling::CountParticipatingPlayers(World);
				Check(LivePlayers == 1, FString::Printf(TEXT("players taking part now: %d (expect 1)"), LivePlayers));

				struct FKind { const TCHAR* Label; const TCHAR* Path; float Multiplier; };
				const FKind Kinds[] = {
					{ TEXT("normal"), nullptr, 1.f },
					{ TEXT("Elite"), TEXT("/Game/MainProject/Contents/Data/Encounters/DA_Encounter_PlaceholderShooterElite.DA_Encounter_PlaceholderShooterElite"), 5.f },
					{ TEXT("Final Level Boss"), TEXT("/Game/MainProject/Contents/Data/Encounters/DA_Encounter_PlaceholderShooterFinalBoss.DA_Encounter_PlaceholderShooterFinalBoss"), 20.f } };
				const float Curve[] = { 1.f, 2.f, 3.25f, 4.75f };
				const int32 Caps[] = { 12, 15, 17, 20 };
				int32 Index = 0;
				for (const FKind& Kind : Kinds)
				{
					const UFPSRLEncounterDefinition* Encounter = Kind.Path ? LoadObject<UFPSRLEncounterDefinition>(nullptr, Kind.Path) : nullptr;
					Check(!Kind.Path || Encounter, FString::Printf(TEXT("%s encounter loaded"), Kind.Label));
					for (int32 Players = 1; Players <= 4; ++Players)
					{
						const FPSRLEnemyScaling::FEncounterScaling Scaling = FPSRLEnemyScaling::Compute(World, Encounter, Players);
						FCase Case;
						Case.Label = Kind.Label;
						Case.Players = Players;
						Case.ExpectedHealth = Base * Curve[Players - 1] * Kind.Multiplier;
						Case.ExpectedMax = Encounter && Encounter->Kind == EFPSRLEncounterKind::FinalLevelBoss ? 1 : Caps[Players - 1];
						Case.ActualMax = Scaling.MaxEnemies;
						Case.Enemy = SpawnEnemy(World, Pawn->GetActorLocation() + FVector(3000.f + 300.f * Index++, 0.f, 3000.f));
						FPSRLEnemyScaling::ApplyToEnemy(Case.Enemy.Get(), Scaling);
						UE_LOG(LogFPSRL, Log, TEXT("[ScalingTest] %s: %s"), Kind.Label, *Scaling.Describe());
						Cases->Add(Case);
					}
				}

				// Infinite scaling: 10 points = +10% health and +10% damage.
				GetMutableDefault<UFPSRLEnemyScalingSettings>()->InfiniteScalingPoints = 10;
				const FPSRLEnemyScaling::FEncounterScaling Deep = FPSRLEnemyScaling::Compute(World, nullptr, 1);
				GetMutableDefault<UFPSRLEnemyScalingSettings>()->InfiniteScalingPoints = 0;
				*DamageEnemy = SpawnEnemy(World, Pawn->GetActorLocation() + FVector(-3000.f, 0.f, 3000.f));
				FPSRLEnemyScaling::ApplyToEnemy(DamageEnemy->Get(), Deep);
				Check(FMath::IsNearlyEqual(Deep.GetHealthMultiplier(), 1.1f, 0.001f) && FMath::IsNearlyEqual(Deep.GetDamageMultiplier(), 1.1f, 0.001f),
					FString::Printf(TEXT("10 infinite-scaling points: health x%.3f, damage x%.3f (expect 1.100 / 1.100)"), Deep.GetHealthMultiplier(), Deep.GetDamageMultiplier()));
				return true;
			}
			switch (*Step)
			{
			case 1002:
				for (const FCase& Case : *Cases)
				{
					const UFPSRLHealthComponent* Health = Case.Enemy.IsValid() ? Case.Enemy->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
					const float Max = Health ? Health->GetMaxHealth() : -1.f;
					const float Current = Health ? Health->GetCurrentHealth() : -1.f;
					Check(FMath::IsNearlyEqual(Max, Case.ExpectedHealth, 0.01f) && FMath::IsNearlyEqual(Current, Case.ExpectedHealth, 0.01f),
						FString::Printf(TEXT("%-16s %d player(s): health %.2f / %.2f (expect %.2f)"), *Case.Label, Case.Players, Current, Max, Case.ExpectedHealth));
					Check(Case.ActualMax == Case.ExpectedMax, FString::Printf(TEXT("%-16s %d player(s): max enemies %d (expect %d)"), *Case.Label, Case.Players, Case.ActualMax, Case.ExpectedMax));
					if (Case.Enemy.IsValid())
					{
						Case.Enemy->Destroy();
					}
				}
				if (const UFPSRLHealthComponent* Health = DamageEnemy->IsValid() ? (*DamageEnemy)->FindComponentByClass<UFPSRLHealthComponent>() : nullptr)
				{
					const UFPSRLEnemyDefinition* Definition = UFPSRLEnemyScalingSettings::Get().FindDefinition((*DamageEnemy)->GetClass());
					const float Want = (Definition ? Definition->BaseHealth : 100.f) * 1.1f;
					Check(FMath::IsNearlyEqual(Health->GetMaxHealth(), Want, 0.01f), FString::Printf(TEXT("infinite-scaled enemy health %.2f (expect %.2f)"), Health->GetMaxHealth(), Want));
				}
				*PlayerHealthBefore = PlayerHealth->GetCurrentHealth();
				UGameplayStatics::ApplyDamage(Pawn, 10.f, nullptr, DamageEnemy->Get(), UDamageType::StaticClass());	// the enemy hits the player for 10
				break;
			case 1004:
			{
				const float Taken = *PlayerHealthBefore - PlayerHealth->GetCurrentHealth();
				Check(FMath::IsNearlyEqual(Taken, 11.f, 0.01f), FString::Printf(TEXT("infinite-scaled enemy's 10 damage hit the player for %.2f (expect 11.00)"), Taken));
				UE_LOG(LogFPSRL, Log, TEXT("[ScalingTest] done: %d problem(s)"), *Problems);
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
