// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.HealthBarTest checks the overhead enemy health bars on this machine (host or client): an ordinary
// enemy's bar appears once hurt, follows its health and goes away when it dies; a Miniboss and a Final Level Boss (rank
// tags, replicated) never show one even when hurt; with fpsrl.EnemyHealthBars 2 a fresh enemy shows it at full health.

#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "UI/FPSRLEnemyHealthBar.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLHealthBarTest
{
	/** Enemies nearest first: distance, health, max, bar shown, bar fraction, bar height above the enemy. */
	struct FEnemyView
	{
		float Distance = 0.f;
		float Health = 0.f;
		float Max = 0.f;
		bool bHasBar = false;
		bool bShown = false;
		float Fraction = -1.f;
		float BarHeight = 0.f;
	};

	TArray<FEnemyView> ViewEnemies(UWorld* World, const APawn* Player)
	{
		TArray<FEnemyView> Views;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const UFPSRLHealthComponent* Health = It->IsPlayerControlled() || *It == Player ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
			if (!Health)
			{
				continue;
			}
			FEnemyView& View = Views.AddDefaulted_GetRef();
			View.Distance = FVector::Dist2D(It->GetActorLocation(), Player->GetActorLocation());
			View.Health = Health->GetCurrentHealth();
			View.Max = Health->GetMaxHealth();
			if (const UFPSRLEnemyHealthBarComponent* Bar = It->FindComponentByClass<UFPSRLEnemyHealthBarComponent>())
			{
				View.bHasBar = true;
				View.bShown = Bar->IsBarShown() && !Bar->bHiddenInGame && Bar->IsWidgetDrawn();
				View.Fraction = Bar->GetShownFraction();
				View.BarHeight = Bar->GetComponentLocation().Z - It->GetActorLocation().Z;
			}
		}
		Views.Sort([](const FEnemyView& A, const FEnemyView& B) { return A.Distance < B.Distance; });
		return Views;
	}

	void Press(AFPSRLPlayerController* PC, const TCHAR* ActionPath)
	{
		const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		if (UInputAction* Action = LoadObject<UInputAction>(nullptr, ActionPath); Action && Input)
		{
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
		}
	}
}

static FAutoConsoleCommandWithWorld GFPSRLHealthBarTestCommand(TEXT("FPSRL.HealthBarTest"),
	TEXT("Test: overhead enemy health bars on this machine; Minibosses and bosses have none ([HealthBarTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Problems](float)
		{
			using namespace FPSRLHealthBarTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[HealthBarTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto Describe = [](const FEnemyView& View)
			{
				return FString::Printf(TEXT("%.0f cm: %.0f/%.0f hp, bar %s%s %.2f, %.0f cm above"), View.Distance, View.Health, View.Max,
					View.bHasBar ? TEXT("present ") : TEXT("MISSING "), View.bShown ? TEXT("shown") : TEXT("hidden"), View.Fraction, View.BarHeight);
			};
			const TCHAR* Shoot = TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot");

			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				AFPSRLPlayerController::GiveWeaponToPawn(Pawn, LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C")));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("450"), TEXT("Miniboss"));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("650"), TEXT("Boss"));
				return true;
			}
			const TArray<FEnemyView> Views = ViewEnemies(World, Pawn);
			switch (*Step)
			{
			case 1008:
				Check(Views.Num() == 3 && Views[0].bHasBar && !Views[0].bShown, FString::Printf(TEXT("ordinary enemy at full health: %s (expect a hidden bar)"), Views.Num() > 0 ? *Describe(Views[0]) : TEXT("none")));
				Check(Views.Num() == 3 && Views[1].Health < Views[1].Max && !Views[1].bShown, FString::Printf(TEXT("hurt Miniboss: %s (expect no bar shown)"), Views.Num() > 1 ? *Describe(Views[1]) : TEXT("none")));
				Check(Views.Num() == 3 && Views[2].Health < Views[2].Max && !Views[2].bShown, FString::Printf(TEXT("hurt Final Level Boss: %s (expect no bar shown)"), Views.Num() > 2 ? *Describe(Views[2]) : TEXT("none")));
				Press(PC, Shoot);
				break;
			case 1012:
				Check(Views.Num() > 0 && Views[0].bShown && FMath::IsNearlyEqual(Views[0].Fraction, Views[0].Health / Views[0].Max, 0.01f) && Views[0].Health < Views[0].Max && Views[0].BarHeight > 90.f,
					FString::Printf(TEXT("ordinary enemy after one shot: %s (expect shown, matching its health, above its head)"), Views.Num() > 0 ? *Describe(Views[0]) : TEXT("none")));
				Press(PC, Shoot);
				break;
			case 1015:
			case 1018:
			case 1021:
				Press(PC, Shoot);	// until it dies (30 per shot)
				break;
			case 1025:
				Check(Views.Num() > 0 && Views[0].Health <= 0.f && !Views[0].bShown, FString::Printf(TEXT("ordinary enemy killed: %s (expect hidden)"), Views.Num() > 0 ? *Describe(Views[0]) : TEXT("gone (bar gone with it)")));
				IConsoleManager::Get().FindConsoleVariable(TEXT("fpsrl.EnemyHealthBars"))->Set(2);
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("250"), TEXT("-150"));
				break;
			case 1031:
			{
				const FEnemyView* Fresh = Views.FindByPredicate([](const FEnemyView& View) { return View.Health > 0.f && View.Health >= View.Max && View.bHasBar; });
				Check(Fresh && Fresh->bShown, FString::Printf(TEXT("fpsrl.EnemyHealthBars 2, fresh enemy at full health: %s (expect shown)"), Fresh ? *Describe(*Fresh) : TEXT("none")));
				UE_LOG(LogFPSRL, Log, TEXT("[HealthBarTest] done: %d problem(s)"), *Problems);
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
