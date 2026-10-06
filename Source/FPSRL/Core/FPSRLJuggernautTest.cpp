// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.JuggernautTest (host, during a real run that reaches a Ground Juggernaut arena, e.g. Lobby +
// fpsrl.Depth.ForceMinibossRoom DA_Room_Z1_Miniboss_01 + fpsrl.Autopilot.FightSeconds 300 + FPSRLAutoRun). Every player
// gets god mode (the mechanics, not the damage). Once the encounter is on ([JuggernautTest]):
//  - its shield is up from the start (25 % damage), 3 thresholds; it never moves; the HUD shows SHIELD UP | HP and the
//    75/50/25 markers
//  - its attack, in its sight: 3 slow shots, then the aim line, then the heavy shot
//  - out of its sight for 4 s: a forced pull - mines thrown, the pull, a quick shockwave, no platforms lit, the shields
//    untouched, the shockwave (after 1.6 s) sets off the ring of mines, back to normal
//  - each threshold (a huge hit is held there): shield up, a ring of 16 mines thrown out round it first (in the air,
//    then landed: never on a platform, never under a player, never more than the cap), the host brought next to the
//    boss, one platform lights per player, the shockwave goes out and sets the mines off; on the lit platform Shield Disruption charges while the boss keeps
//    shooting; stepping off pauses it (progress kept), back on resumes; full = the shield breaks: 150 % damage until the
//    next threshold. After the third: no floor, it can be killed.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "AI/FPSRLShieldEncounterComponent.h"
#include "Combat/FPSRLLandmine.h"
#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
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
		int32 ShotsBefore = 0;
		bool bAimLineSeen = false;
		int32 MaxMines = 0;
		int32 MinesSeen = 0;
		int32 MinesFlying = 0;
		int32 BadMines = 0;
		TSet<TWeakObjectPtr<AFPSRLLandmine>> MinesSeenSet;
		TSet<TWeakObjectPtr<AFPSRLLandmine>> MinesLanded;
		bool bShockwaveSeen = false;
		TSet<int32> Shots;
		/** Rendered runs: one screenshot (with the HUD) per moment, looking at Look. */
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

	/** Moves the player to a spot on the navmesh, away from mines and the other players, where the boss can (bVisible) or
	 *  can't see them - asked of its own sight check, so the test agrees with the forced pull's. */
	static bool MoveToSpot(UWorld* World, ACharacter* Boss, ACharacter* Player, bool bVisible, const TArray<FVector>& Taken)
	{
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(Boss->GetController());
		if (!Nav || !AI)
		{
			return false;
		}
		const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const TArray<AFPSRLLandmine*> Mines = AFPSRLLandmine::GetMinesOf(Boss);
		for (float Distance : { 1200.f, 1800.f, 2400.f, 3000.f, 3600.f })
		{
			for (int32 Angle = 0; Angle < 360; Angle += 15)
			{
				const FVector Wanted = Boss->GetActorLocation() + FRotator(0.f, Angle, 0.f).Vector() * Distance;
				FNavLocation Point;
				if (!Nav->ProjectPointToNavigation(Wanted, Point, FVector(300.f, 300.f, 1500.f)))
				{
					continue;
				}
				const FVector Body = Point.Location + FVector(0.f, 0.f, Half + 5.f);
				const bool bClear = !Mines.ContainsByPredicate([&Body](const AFPSRLLandmine* Mine) { return FVector::Dist2D(Mine->GetDestination(), Body) < Mine->GetTriggerRadius() + 200.f; })
					&& !Taken.ContainsByPredicate([&Body](const FVector& Other) { return FVector::Dist2D(Other, Body) < 150.f; });
				if (!bClear || !Player->TeleportTo(Body, (Boss->GetActorLocation() - Body).Rotation()))
				{
					continue;
				}
				if (AI->HasLineOfSightTo(Player) == bVisible)
				{
					Player->GetCharacterMovement()->StopMovementImmediately();
					return true;
				}
			}
		}
		return false;
	}

	/** Every player to a spot the boss can (or can't) see. */
	static bool MoveEveryone(UWorld* World, ACharacter* Boss, bool bVisible)
	{
		bool bAll = true;
		TArray<FVector> Taken;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			ACharacter* Player = It->Get() ? Cast<ACharacter>(It->Get()->GetPawn()) : nullptr;
			bAll &= Player && MoveToSpot(World, Boss, Player, bVisible, Taken);
			if (Player)
			{
				Taken.Add(Player->GetActorLocation());
			}
		}
		return bAll;
	}

	static int32 MinesInFlight(const APawn* Boss)
	{
		int32 Count = 0;
		for (const AFPSRLLandmine* Mine : AFPSRLLandmine::GetMinesOf(Boss))
		{
			Count += Mine->HasLanded() ? 0 : 1;
		}
		return Count;
	}
}

static FAutoConsoleCommandWithWorld GFPSRLJuggernautTestCommand(TEXT("FPSRL.JuggernautTest"),
	TEXT("Test (host, a run into a Ground Juggernaut arena): shields, the volley, the forced pull, thrown mines, the platform mechanic ([JuggernautTest])."),
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
			const AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(Boss->GetController());
			const UFPSRLEnemyRoleComponent* RoleView = Boss->FindComponentByClass<UFPSRLEnemyRoleComponent>();
			UFPSRLHealthComponent* BossHealth = Boss->FindComponentByClass<UFPSRLHealthComponent>();
			const float BossFloor = S->BossStart.Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

			// Every frame: god mode on everyone, the aim line, the mines as they fly and land, the shockwave.
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (AFPSRLPlayerState* PlayerState = It->Get() ? It->Get()->GetPlayerState<AFPSRLPlayerState>() : nullptr)
				{
					PlayerState->bGodMode = true;
				}
			}
			S->bAimLineSeen |= RoleView && RoleView->IsAimLineShowing();
			const TArray<AFPSRLLandmine*> Mines = AFPSRLLandmine::GetMinesOf(Boss);
			S->MaxMines = FMath::Max(S->MaxMines, Mines.Num());
			for (AFPSRLLandmine* Mine : Mines)
			{
				if (!S->MinesSeenSet.Contains(Mine))
				{
					S->MinesSeenSet.Add(Mine);
					++S->MinesSeen;
					S->MinesFlying += Mine->HasLanded() ? 0 : 1;
				}
				if (!Mine->HasLanded() || S->MinesLanded.Contains(Mine))
				{
					continue;
				}
				S->MinesLanded.Add(Mine);
				bool bOnPlatform = Mine->GetActorLocation().Z - BossFloor > 200.f;
				for (const AFPSRLShieldPlatform* Platform : AFPSRLShieldPlatform::FindAround(World, S->BossStart, 6000.f))
				{
					const FVector Local = Platform->GetActorTransform().InverseTransformPosition(Mine->GetActorLocation());
					bOnPlatform |= FMath::Abs(Local.X) <= Platform->HalfSize.X && FMath::Abs(Local.Y) <= Platform->HalfSize.Y;
				}
				bool bUnderPlayer = false;
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					const APawn* Player = It->Get() ? It->Get()->GetPawn() : nullptr;
					bUnderPlayer |= Player && FVector::Dist2D(Mine->GetActorLocation(), Player->GetActorLocation()) < Mine->GetTriggerRadius();
				}
				const bool bAtTarget = FVector::Dist(Mine->GetActorLocation(), Mine->GetDestination()) < 10.f;
				if (bOnPlatform || bUnderPlayer || !bAtTarget)
				{
					++S->BadMines;
					UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] bad mine at %s (on a platform %d, under a player %d, at its target %d)"), *Mine->GetActorLocation().ToCompactString(),
						bOnPlatform, bUnderPlayer, bAtTarget);
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
					UGameplayStatics::ApplyDamage(Boss, 400.f, PC, Host, nullptr);
					const float Taken = Before - BossHealth->GetCurrentHealth();
					S->Check(Shield->GetShieldsRemaining() == 3 && Shield->IsShieldUp() && Taken > 400.f * 0.2f && Taken < 400.f * 0.3f,
						FString::Printf(TEXT("shield up from the start, 3 thresholds: 400 damage took %.0f (%.0f -> %.0f of %.0f)"), Taken, Before, BossHealth->GetCurrentHealth(), BossHealth->GetMaxHealth()));
					S->Check(DescribeHud().StartsWith(TEXT("SHIELD UP | HP")) && DescribeHud().Contains(TEXT("markers 75/50/25")), FString::Printf(TEXT("HUD: %s"), *DescribeHud()));
				}
				S->Check(MoveEveryone(World, Boss, true), TEXT("everyone moved to where it can see them"));
				S->ShotsBefore = AI ? AI->GetProjectilesFired() : 0;
				S->bAimLineSeen = false;
				Next(2);
				break;

			// --- its attack: 3 slow shots, the aim line, the heavy shot ---------------------------------------------------
			case 2:
				if (InStep > 2.0)
				{
					S->Shot(PC, 1, S->BossStart, TEXT("the volley"));
				}
				if ((!AI || AI->GetProjectilesFired() - S->ShotsBefore < 4 || !S->bAimLineSeen) && InStep < 15.0)
				{
					return true;
				}
				S->Check(AI && AI->GetProjectilesFired() - S->ShotsBefore >= 4 && S->bAimLineSeen,
					FString::Printf(TEXT("the volley: %d shot(s) in %.1f s, the aim line before the heavy shot %d"), AI ? AI->GetProjectilesFired() - S->ShotsBefore : 0, InStep, S->bAimLineSeen));
				S->Check(FVector::Dist2D(Boss->GetActorLocation(), S->BossStart) < 50.f, FString::Printf(TEXT("the boss never moved (%.0f cm)"), FVector::Dist2D(Boss->GetActorLocation(), S->BossStart)));
				S->Check(S->MinesSeen == 0, FString::Printf(TEXT("no mines before a pull (%d)"), S->MinesSeen));
				S->Check(MoveEveryone(World, Boss, false), FString::Printf(TEXT("everyone hidden from it (the host at %s)"), *Host->GetActorLocation().ToCompactString()));
				Next(3);
				break;

			// --- the forced pull: nobody in its sight --------------------------------------------------------------------
			case 3:
				if (Shield->GetPhase() == EFPSRLShieldPhase::Idle && InStep < 9.0)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Throwing && Shield->IsForcedPull() && InStep > 3.5 && MinesInFlight(Boss) == 16,
					FString::Printf(TEXT("hidden %.1f s: a forced pull starts with all 16 mines in the air (%d flying; %s)"), InStep, MinesInFlight(Boss), *DescribeHud()));
				Next(4);
				break;
			case 4:
				if (Shield->GetPhase() == EFPSRLShieldPhase::Throwing && InStep < 3.0)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Charging && Shield->GetHighlightedPlatforms().IsEmpty()
					&& FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) < 900.f,
					FString::Printf(TEXT("forced pull after %.1f s: the host pulled in (%.1f m), %d platform(s) lit (none wanted)"), InStep,
						FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) / 100.f, Shield->GetHighlightedPlatforms().Num()));
				S->bShockwaveSeen = false;
				Next(5);
				break;
			case 5:
				if (Shield->GetPhase() == EFPSRLShieldPhase::Charging && InStep < 3.0)
				{
					return true;
				}
				S->Check(S->bShockwaveSeen && InStep > 1.4 && InStep < 2.2 && Shield->GetPhase() == EFPSRLShieldPhase::Idle && Shield->GetShieldsRemaining() == 3 && Shield->IsShieldUp()
					&& Shield->GetHighlightedPlatforms().IsEmpty(),
					FString::Printf(TEXT("forced shockwave after %.1f s, then back to normal: shields %d, no platforms (%s)"), InStep, Shield->GetShieldsRemaining(), *DescribeHud()));
				S->bShockwaveSeen = false;
				Next(6);
				break;
			case 6:	// the shockwave sets the ring of mines off as it passes
				if (InStep < 1.2)
				{
					return true;
				}
				S->Check(Mines.IsEmpty(), FString::Printf(TEXT("forced pull: the shockwave set off the ring of mines (%d left)"), Mines.Num()));
				UGameplayStatics::ApplyDamage(Boss, 100000.f, PC, Host, nullptr);	// to the first threshold
				Next(10);
				break;

			// --- the platform mechanic (twice) -------------------------------------------------------------------------
			case 10:	// the throw first
				if (InStep < 0.3)
				{
					return true;
				}
				{
					const float Expected = Shield->GetThresholds()[S->Breaks] * BossHealth->GetMaxHealth();
					S->Check(FMath::Abs(BossHealth->GetCurrentHealth() - Expected) < 2.f && Shield->IsShieldUp(),
						FString::Printf(TEXT("threshold %d: a huge hit held it at %.0f%% (%.0f), shield up"), S->Breaks + 1, Shield->GetThresholds()[S->Breaks] * 100.f, BossHealth->GetCurrentHealth()));
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Throwing && !Shield->IsForcedPull() && Shield->GetHighlightedPlatforms().IsEmpty()
					&& MinesInFlight(Boss) == 16,
					FString::Printf(TEXT("threshold %d: mines thrown first (%d in the air, %d down), no platform lit yet (%s)"), S->Breaks + 1, MinesInFlight(Boss), Mines.Num(), *DescribeHud()));
				S->Shot(PC, 5 + S->Breaks, S->BossStart, TEXT("the mines thrown out"));
				Next(11);
				break;
			case 11:	// then the pull and the charge
				if (Shield->GetPhase() == EFPSRLShieldPhase::Throwing && InStep < 3.0)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Charging && Shield->GetHighlightedPlatforms().Num() == World->GetNumPlayerControllers()
					&& FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) < 900.f,
					FString::Printf(TEXT("mechanic %d: charging, %d platform(s) lit for %d player(s), the host brought next to the boss (%.1f m)"), S->Breaks + 1,
						Shield->GetHighlightedPlatforms().Num(), World->GetNumPlayerControllers(), FVector::Dist2D(Host->GetActorLocation(), Boss->GetActorLocation()) / 100.f));
				Next(12);
				break;
			case 12:
				if (InStep > 1.0 && Shield->GetPhase() == EFPSRLShieldPhase::Charging)
				{
					S->Shot(PC, 10 + S->Breaks, Shield->GetHighlightedPlatforms().IsEmpty() ? S->BossStart : Shield->GetHighlightedPlatforms()[0]->GetActorLocation(), TEXT("the charge: blast area, the lit platform"));
				}
				if (Shield->GetPhase() == EFPSRLShieldPhase::Charging && InStep < 4.0)
				{
					return true;
				}
				S->Check(S->bShockwaveSeen && Shield->GetPhase() == EFPSRLShieldPhase::AwaitingPlayers, FString::Printf(TEXT("the shockwave went out; waiting for the platform (%s)"), *DescribeHud()));
				S->ShotsBefore = AI ? AI->GetProjectilesFired() : 0;
				EveryoneOn(World, Shield->GetHighlightedPlatforms());
				Next(13);
				break;
			case 13:	// on: charging
				if (InStep > 1.2)
				{
					S->Shot(PC, 20 + S->Breaks, S->BossStart, TEXT("Shield Disruption from the platform"));
				}
				if (InStep < 2.0)
				{
					return true;
				}
				S->Check(Mines.IsEmpty(), FString::Printf(TEXT("mechanic %d: the shockwave set off the ring of mines (%d left)"), S->Breaks + 1, Mines.Num()));
				S->Progress = Shield->GetDisruptionFraction();
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Disrupting && S->Progress > 0.2f, FString::Printf(TEXT("on the lit platform: Shield Disruption %.0f%% (%s)"), S->Progress * 100.f, *DescribeHud()));
				Host->TeleportTo(S->BossStart + (Host->GetActorLocation() - S->BossStart).GetSafeNormal2D() * 1000.f, Host->GetActorRotation());	// step off
				Next(14);
				break;
			case 14:	// off: paused, kept
				if (InStep < 1.0)
				{
					return true;
				}
				S->Check(Shield->GetPhase() == EFPSRLShieldPhase::AwaitingPlayers && FMath::Abs(Shield->GetDisruptionFraction() - S->Progress) < 0.05f,
					FString::Printf(TEXT("off the platform: paused at %.0f%% (was %.0f%%)"), Shield->GetDisruptionFraction() * 100.f, S->Progress * 100.f));
				EveryoneOn(World, Shield->GetHighlightedPlatforms());
				Next(15);
				break;
			case 15:	// back on: resumes and breaks a layer
				if (Shield->GetShieldsRemaining() == 3 - S->Breaks && InStep < 8.0)
				{
					return true;
				}
				S->Check(AI && AI->GetProjectilesFired() > S->ShotsBefore, FString::Printf(TEXT("it kept shooting during Shield Disruption (%d shot(s))"), AI ? AI->GetProjectilesFired() - S->ShotsBefore : 0));
				++S->Breaks;
				S->Check(Shield->GetShieldsRemaining() == 3 - S->Breaks && !Shield->IsShieldUp() && DescribeHud().StartsWith(TEXT("SHIELD DOWN")), FString::Printf(TEXT("back on: resumed and broke shield %d after %.1f s (%d left; %s)"), S->Breaks, InStep, Shield->GetShieldsRemaining(), *DescribeHud()));
				Next(S->Breaks < 3 ? 16 : 20);
				break;
			case 16:	// shield down until the next threshold: increased damage; then on to it
				if (InStep < 0.5)
				{
					return true;
				}
				{
					const float Before = BossHealth->GetCurrentHealth();
					UGameplayStatics::ApplyDamage(Boss, 100.f, PC, Host, nullptr);
					const float Taken = Before - BossHealth->GetCurrentHealth();
					S->Check(Taken > 140.f && Taken < 160.f && Shield->GetPhase() == EFPSRLShieldPhase::Idle, FString::Printf(TEXT("shield down: 100 damage took %.0f"), Taken));
				}
				S->bShockwaveSeen = false;
				UGameplayStatics::ApplyDamage(Boss, 100000.f, PC, Host, nullptr);	// to the next threshold
				Next(10);
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
					const float Taken = Before - BossHealth->GetCurrentHealth();
					S->Check(Shield->GetPhase() == EFPSRLShieldPhase::Vulnerable && Taken > 280.f && Taken < 320.f,
						FString::Printf(TEXT("all three thresholds done: shield down for good, 200 damage took %.0f (%.0f -> %.0f)"), Taken, Before, BossHealth->GetCurrentHealth()));
					S->Check(DescribeHud().StartsWith(TEXT("SHIELD DOWN")), FString::Printf(TEXT("HUD: %s"), *DescribeHud()));
					UGameplayStatics::ApplyDamage(Boss, 100000.f, PC, Host, nullptr);
					S->Check(BossHealth->IsDead(), TEXT("no floor any more: it can be killed"));
				}
				S->Check(S->MinesSeen > 0 && S->MinesFlying == S->MinesSeen && S->MaxMines <= 32 && S->BadMines == 0,
					FString::Printf(TEXT("mines: %d thrown (%d seen in the air), at most %d down at once (cap 32), %d landed badly"), S->MinesSeen, S->MinesFlying, S->MaxMines, S->BadMines));
				UE_LOG(LogFPSRL, Log, TEXT("[JuggernautTest] done: %d problem(s)"), S->Problems);
				return false;
			default:
				break;
			}
			return true;
		}));
	}));
#endif
