// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.JuggernautTest (host, solo, during a real run that reaches a Ground Juggernaut arena, e.g. Lobby +
// fpsrl.Depth.ForceMinibossRoom DA_Room_Z1_Miniboss_01 + fpsrl.Autopilot.FightSeconds 300 + FPSRLAutoRun). Once the
// encounter is on ([JuggernautTest]):
//  - two shield layers, the boss can't be hurt, it never moves; the HUD shows SHIELD 1 | SHIELD 2 | HP
//  - Grounded Missiles mark the host and go off; landmines go down (never on a platform, never under the host, never
//    more than the cap); one stepped on goes off
//  - the platform mechanic: the host is brought next to the boss, one platform lights (solo), the shockwave goes out;
//    on the lit platform Shield Disruption charges; stepping off pauses it (progress kept), back on resumes; full = a
//    layer breaks. Twice: then it can be hurt, the mechanic is over, its mines stay.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLShieldEncounterComponent.h"
#include "Combat/FPSRLGroundStrike.h"
#include "Combat/FPSRLLandmine.h"
#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Rooms/FPSRLShieldPlatform.h"
#include "UI/FPSRLEncounterBarWidget.h"
#include "UObject/UObjectIterator.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLJuggernautTest
{
	struct FState
	{
		int32 Step = 0;
		double StepTime = 0.0;
		TWeakObjectPtr<ACharacter> Boss;
		FVector BossStart = FVector::ZeroVector;
		float Progress = 0.f;
		int32 Breaks = 0;
		TSet<TWeakObjectPtr<AFPSRLGroundStrike>> Strikes;
		int32 StrikeHits = 0;
		int32 MaxMines = 0;
		int32 MinesSeen = 0;
		int32 BadMines = 0;
		TSet<TWeakObjectPtr<AFPSRLLandmine>> MinesChecked;
		bool bShockwaveSeen = false;
		TWeakObjectPtr<AFPSRLLandmine> SteppedOn;
		TSet<int32> Shots;
		/** Rendered runs: one screenshot (with the HUD) per moment, looking from Eye at Look. */
		void Shot(APlayerController* PC, int32 Key, const FVector& Look, const TCHAR* Moment)
		{
			if (Shots.Contains(Key) || !PC->GetPawn())
			{
				return;
			}
			Shots.Add(Key);
			PC->SetControlRotation((Look - PC->GetPawn()->GetPawnViewLocation()).Rotation());
			PC->ConsoleCommand(TEXT("Shot showui"));
			UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] screenshot: %s"), Moment);
		}
		int32 Problems = 0;
		void Check(bool bOk, const FString& What)
		{
			Problems += bOk ? 0 : 1;
			UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
	};

	static FString DescribeHud()
	{
		for (TObjectIterator<UFPSRLEncounterBarWidget> It; It; ++It)
		{
			if (It->IsInViewport())
			{
				return It->Describe();
			}
		}
		return TEXT("(no encounter bar on screen)");
	}

	static void PutOn(ACharacter* Player, const AActor* Platform)
	{
		Player->TeleportTo(Platform->GetActorLocation() + FVector(0.f, 0.f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Player->GetActorRotation());
	}

	/** Every player in the game, each onto their own lit platform. */
	static void EveryoneOn(UWorld* World, const TArray<AFPSRLShieldPlatform*>& Lit)
	{
		int32 Index = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It && Lit.Num() > 0; ++It)
		{
			if (ACharacter* Player = It->Get() ? Cast<ACharacter>(It->Get()->GetPawn()) : nullptr)
			{
				PutOn(Player, Lit[FMath::Min(Index++, Lit.Num() - 1)]);
			}
		}
	}
}

static FAutoConsoleCommandWithWorld GFPSRLJuggernautTestCommand(TEXT("FPSRL.JuggernautTest"),
	TEXT("Test (host, solo, a run into a Ground Juggernaut arena): shields, missiles, mines, the platform mechanic ([JuggernautTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		using namespace FPSRLJuggernautTest;
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FState> S = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			ACharacter* Host = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (!Host)
			{
				return true;
			}
			const double Now = World->GetTimeSeconds();
			const double InStep = Now - S->StepTime;
			auto Next = [&S, Now](int32 Step) { S->Step = Step; S->StepTime = Now; };

			// Find the boss once its encounter is on.
			ACharacter* Boss = S->Boss.Get();
			UFPSRLShieldEncounterComponent* Shield = Boss ? Boss->FindComponentByClass<UFPSRLShieldEncounterComponent>() : nullptr;
			if (S->Step == 0)
			{
				for (TActorIterator<ACharacter> It(World); It; ++It)
				{
					UFPSRLShieldEncounterComponent* Found = It->FindComponentByClass<UFPSRLShieldEncounterComponent>();
					if (Found && Found->GetShieldLayers() > 0)
					{
						S->Boss = *It;
						S->BossStart = It->GetActorLocation();
						UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] found %s"), *It->GetName());
						Next(1);
					}
				}
				return true;
			}
			if (!Boss || !Shield)
			{
				S->Check(false, TEXT("the boss is gone"));
				UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] done: %d problem(s)"), S->Problems);
				return false;
			}
			UFPSRLHealthComponent* BossHealth = Boss->FindComponentByClass<UFPSRLHealthComponent>();
			const float BossFloor = S->BossStart.Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

			// Every frame: missiles and mines as they appear.
			for (TActorIterator<AFPSRLGroundStrike> It(World); It; ++It)
			{
				if (It->GetInstigator() == Boss)
				{
					S->Strikes.Add(*It);
				}
			}
			const TArray<AFPSRLLandmine*> Mines = AFPSRLLandmine::GetMinesOf(Boss);
			S->MaxMines = FMath::Max(S->MaxMines, Mines.Num());
			for (AFPSRLLandmine* Mine : Mines)
			{
				if (S->MinesChecked.Contains(Mine))
				{
					continue;
				}
				S->MinesChecked.Add(Mine);
				++S->MinesSeen;
				bool bOnPlatform = Mine->GetActorLocation().Z - BossFloor > 200.f;
				for (const AFPSRLShieldPlatform* Platform : AFPSRLShieldPlatform::FindAround(World, S->BossStart, 6000.f))
				{
					const FVector Local = Platform->GetActorTransform().InverseTransformPosition(Mine->GetActorLocation());
					bOnPlatform |= FMath::Abs(Local.X) <= Platform->HalfSize.X && FMath::Abs(Local.Y) <= Platform->HalfSize.Y;
				}
				const bool bUnderHost = FVector::Dist2D(Mine->GetActorLocation(), Host->GetActorLocation()) < Mine->GetTriggerRadius();
				if (bOnPlatform || bUnderHost)
				{
					++S->BadMines;
					UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] bad mine at %s (on a platform %d, under the host %d)"), *Mine->GetActorLocation().ToCompactString(), bOnPlatform, bUnderHost);
				}
			}
			for (TActorIterator<AFPSRLShockwave> It(World); It && !S->bShockwaveSeen; ++It)
			{
				S->bShockwaveSeen = It->GetInstigator() == Boss;
			}

			switch (S->Step)
			{
			case 1:	// shields and stance
				if (InStep < 3.0)
				{
					return true;
				}
				{
					const float Before = BossHealth->GetCurrentHealth();
					UGameplayStatics::ApplyDamage(Boss, 500.f, PC, Host, nullptr);
					S->Check(Shield->GetShieldsRemaining() == 2 && BossHealth->IsInvulnerable() && FMath::IsNearlyEqual(BossHealth->GetCurrentHealth(), Before),
						FString::Printf(TEXT("2 shields up, 500 damage did nothing (%.0f -> %.0f of %.0f)"), Before, BossHealth->GetCurrentHealth(), BossHealth->GetMaxHealth()));
					S->Check(DescribeHud().StartsWith(TEXT("SHIELD 1 | SHIELD 2 | HP")), FString::Printf(TEXT("HUD: %s"), *DescribeHud()));
				}
				Next(2);
				break;
			case 2:	// missiles and mines for a while
				if (InStep > 9.0 && InStep < 9.3 && !S->Shots.Contains(1))
				{
					Host->TeleportTo(S->BossStart + FVector(-2300.f, -900.f, 1500.f), Host->GetActorRotation(), false, true);
					Host->GetCharacterMovement()->SetMovementMode(MOVE_Flying);	// hold the view a moment
				}
				if (InStep > 9.5)
				{
					S->Shot(PC, 1, S->BossStart, TEXT("arena overview mid-fight (marks, mines)"));
				}
				if (InStep > 10.0 && Host->GetCharacterMovement()->MovementMode == MOVE_Flying)
				{
					Host->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
				}
				if (InStep < 14.0)
				{
					return true;
				}
				S->Check(S->Strikes.Num() > 0, FString::Printf(TEXT("Grounded Missiles: %d mark(s) so far"), S->Strikes.Num()));
				S->Check(S->MinesSeen > 0 && S->MaxMines <= 8 && S->BadMines == 0,
					FString::Printf(TEXT("mines: %d laid, at most %d down at once (cap 8), %d on a platform / under the host"), S->MinesSeen, S->MaxMines, S->BadMines));
				S->Check(FVector::Dist2D(Boss->GetActorLocation(), S->BossStart) < 50.f, FString::Printf(TEXT("the boss never moved (%.0f cm)"), FVector::Dist2D(Boss->GetActorLocation(), S->BossStart)));
				// Step on a mine (armed).
				for (AFPSRLLandmine* Mine : Mines)
				{
					if (Mine->IsArmed())
					{
						S->SteppedOn = Mine;
						Host->TeleportTo(Mine->GetActorLocation() + FVector(0.f, 0.f, Host->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Host->GetActorRotation());
						break;
					}
				}
				S->MaxMines = Mines.Num();
				Next(3);
				break;
			case 3:
				if (InStep < 1.0)
				{
					return true;
				}
				S->Check(!S->SteppedOn.IsValid() && S->MaxMines > 0, FString::Printf(TEXT("stepping on an armed mine set it off (it is gone; %d down now)"), Mines.Num()));
				Shield->StartMechanicNow();
				Next(10);
				break;

			// --- the platform mechanic (twice) -------------------------------------------------------------------------
			case 10:
				if (InStep < 0.3)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Charging && Shield->GetHighlightedPlatforms().Num() == World->GetNumPlayerControllers()
					&& FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) < 900.f,
					FString::Printf(TEXT("mechanic %d: charging, %d platform(s) lit for %d player(s), the host brought next to the boss (%.1f m)"), S->Breaks + 1,
						Shield->GetHighlightedPlatforms().Num(), World->GetNumPlayerControllers(), FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) / 100.f));
				Next(11);
				break;
			case 11:
				if (InStep > 1.0 && Shield->GetPhase() == EFPSRLShieldPhase::Charging)
				{
					S->Shot(PC, 10 + S->Breaks, Shield->GetHighlightedPlatforms().IsEmpty() ? S->BossStart : Shield->GetHighlightedPlatforms()[0]->GetActorLocation(), TEXT("the charge: blast area, the lit platform"));
				}
				if (Shield->GetPhase() == EFPSRLShieldPhase::Charging && InStep < 4.0)
				{
					return true;
				}
				S->Check(S->bShockwaveSeen && Shield->GetPhase() == EFPSRLShieldPhase::AwaitingPlayers, FString::Printf(TEXT("the shockwave went out; waiting for the platform (%s)"), *DescribeHud()));
				EveryoneOn(World, Shield->GetHighlightedPlatforms());
				Next(12);
				break;
			case 12:	// on: charging
				if (InStep > 1.2)
				{
					S->Shot(PC, 20 + S->Breaks, S->BossStart, TEXT("Shield Disruption from the platform"));
				}
				if (InStep < 2.0)
				{
					return true;
				}
				S->Progress = Shield->GetDisruptionFraction();
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Disrupting && S->Progress > 0.2f, FString::Printf(TEXT("on the lit platform: Shield Disruption %.0f%% (%s)"), S->Progress * 100.f, *DescribeHud()));
				Host->TeleportTo(S->BossStart + (Host->GetActorLocation() - S->BossStart).GetSafeNormal2D() * 1000.f, Host->GetActorRotation());	// step off
				Next(13);
				break;
			case 13:	// off: paused, kept
				if (InStep < 1.0)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::AwaitingPlayers && FMath::Abs(Shield->GetDisruptionFraction() - S->Progress) < 0.05f,
					FString::Printf(TEXT("off the platform: paused at %.0f%% (was %.0f%%)"), Shield->GetDisruptionFraction() * 100.f, S->Progress * 100.f));
				EveryoneOn(World, Shield->GetHighlightedPlatforms());
				Next(14);
				break;
			case 14:	// back on: resumes and breaks a layer
				if (Shield->GetShieldsRemaining() == 2 - S->Breaks && InStep < 8.0)
				{
					return true;
				}
				++S->Breaks;
				S->Check(Shield->GetShieldsRemaining() == 2 - S->Breaks, FString::Printf(TEXT("back on: resumed and broke shield %d after %.1f s (%d left; %s)"), S->Breaks, InStep, Shield->GetShieldsRemaining(), *DescribeHud()));
				if (S->Breaks < 2)
				{
					Shield->StartMechanicNow();
					Next(10);
					break;
				}
				Next(20);
				break;

			// --- vulnerable ------------------------------------------------------------------------------------------------
			case 20:
				S->Shot(PC, 30, S->BossStart, TEXT("vulnerable"));
				if (InStep < 0.5)
				{
					return true;
				}
				{
					const float Before = BossHealth->GetCurrentHealth();
					UGameplayStatics::ApplyDamage(Boss, 200.f, PC, Host, nullptr);
					S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Vulnerable && !BossHealth->IsInvulnerable() && BossHealth->GetCurrentHealth() < Before,
						FString::Printf(TEXT("both shields broken: vulnerable, 200 damage lands (%.0f -> %.0f)"), Before, BossHealth->GetCurrentHealth()));
					S->Check(DescribeHud().Contains(TEXT("SHIELD 2  BROKEN")), FString::Printf(TEXT("HUD: %s"), *DescribeHud()));
					S->Check(AFPSRLLandmine::GetMinesOf(Boss).Num() > 0 || S->MinesSeen > 0, FString::Printf(TEXT("its mines stay after the shields (%d down)"), AFPSRLLandmine::GetMinesOf(Boss).Num()));
				}
				UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] done: %d problem(s)"), S->Problems);
				return false;
			default:
				break;
			}
			return true;
		}));
	}));
#endif
