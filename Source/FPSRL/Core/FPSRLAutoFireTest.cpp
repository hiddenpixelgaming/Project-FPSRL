// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.AutoFireTest <distance cm> holds the rifle's trigger for 2 s at a very tough enemy and compares bullets
// fired with bullets that landed (projectiles must not block each other: a fast weapon's bullets used to pile up on the
// physics spheres earlier bullets leave behind). The rest of the misses are the weapon's spread. FPSRL.AutoFireTest <cm> aim
// does the same aiming down sights, and also reports how far the view climbed (recoil) and the shots per second. FPSRL.AutoFireTest <cm> aim
// does the same aiming down sights, and also reports how far the view climbed (recoil) and the shots per second.

#include "Combat/FPSRLWeapon.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFPSRLAutoFireTestCommand(TEXT("FPSRL.AutoFireTest"),
	TEXT("Host test: rifle full auto for 2 s at an enemy; bullets fired vs landed ([AutoFireTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		const FString Distance = Args.IsEmpty() ? FString(TEXT("800")) : Args[0];
		const bool bAim = Args.Num() > 1 && Args[1] == TEXT("aim");
		TSharedRef<float> PitchBefore = MakeShared<float>(0.f);
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> AmmoBefore = MakeShared<int32>(0);
		TSharedRef<float> HealthBefore = MakeShared<float>(0.f);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, AmmoBefore, HealthBefore, Distance, bAim, PitchBefore](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			AFPSRLWeapon* Weapon = nullptr;
			for (TActorIterator<AFPSRLWeapon> It(World); It && !Weapon; ++It)
			{
				Weapon = It->GetOwner() == Pawn && !It->IsHidden() ? *It : nullptr;
			}
			UFPSRLHealthComponent* EnemyHealth = nullptr;
			for (TActorIterator<APawn> It(World); It && !EnemyHealth; ++It)
			{
				EnemyHealth = It->IsPlayerControlled() ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
			}
			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				AFPSRLPlayerController::GiveWeaponToPawn(Pawn, LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C")));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), Distance, FString());
				return true;
			}
			if (*Step == 1004 && Weapon && bAim)
			{
				Weapon->StartAiming();	// what the character's aim input does
			}
			if (*Step == 1006 && Weapon && EnemyHealth)
			{
				EnemyHealth->SetMaxHealthServer(100000.f);	// never dies: every bullet that lands counts
				*AmmoBefore = Weapon->CurrentBullets;
				*HealthBefore = EnemyHealth->GetCurrentHealth();
				*PitchBefore = PC->GetControlRotation().Pitch;
				Weapon->StartFiring();
			}
			else if (*Step == 1026 && Weapon)
			{
				Weapon->StopFiring();
			}
			else if (*Step == 1034 && Weapon && EnemyHealth)
			{
				const int32 Fired = *AmmoBefore - Weapon->CurrentBullets;
				const float Damage = *HealthBefore - EnemyHealth->GetCurrentHealth();
				const int32 Landed = FMath::RoundToInt(Damage / FMath::Max(1.f, Weapon->ShotDamage));
				UE_LOG(LogFPSRL, Log, TEXT("[AutoFireTest] %s %s cm%s: fired %d, landed %d (%.0f damage at %.0f per bullet)"),
					Landed >= Fired * 0.6f ? TEXT("OK     ") : TEXT("PROBLEM"), *Distance, bAim ? TEXT(" aiming") : TEXT(""), Fired, Landed, Damage, Weapon->ShotDamage);
				UE_LOG(LogFPSRL, Log, TEXT("[AutoFireTest] aiming %d: %.1f shots per second, view climbed %.2f degrees, refire %.3f s"),
					Weapon->IsWeaponAiming() ? 1 : 0, Fired / 2.f, FRotator::NormalizeAxis(PC->GetControlRotation().Pitch - *PitchBefore), Weapon->GetRefireRate());
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.1f);
	}));
#endif
