// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.RoleBehaviourTest (host, solo, Lobby; no god mode). The playtest v0.1.41 role behaviours ([RoleBehaviour]):
//  1. Brute 13 m away: roars, leaps to the player, the landing shockwave hurts a player standing on the ground, it
//     pauses ~1 s, then rushes (runs at) the nearest player.
//  2. Shockwave: a player who jumps as it arrives takes nothing; one who stays on the ground takes its damage.
//  3. Skirmisher 9 m away: sprints in; a player's bullet hurts it and makes it leap back and phase; a second bullet while
//     phased passes and does nothing; melee while phased still hurts; it phases back in after its PhaseSeconds.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyAnimInstance.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "Combat/FPSRLCombatRules.h"
#include "Combat/FPSRLProjectile.h"
#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLRoleBehaviourTest
{
	struct FState
	{
		int32 Step = 0;
		double StepTime = 0.0;
		TWeakObjectPtr<ACharacter> Enemy;
		TWeakObjectPtr<AFPSRLShockwave> Wave;
		float Health = 0.f;
		float MaxSpeed = 0.f;
		double LandedTime = 0.0;
		float SpeedAfterLanding = 0.f;	// ~0.3-0.8 s after: should be standing (the pause)
		float RushSpeed = 0.f;			// 1.3-3 s after: running
		bool bSawAction = false;
		bool bNav = false;
		double PhaseStart = 0.0;
		TSet<FString> Shots;
		/** Rendered runs: one screenshot per moment (Saved/Screenshots). */
		void Shot(APlayerController* PC, const TCHAR* Moment)
		{
			if (!Shots.Contains(Moment))
			{
				Shots.Add(Moment);
				PC->ConsoleCommand(TEXT("Shot"));
				UE_LOG(LogFPSRL, Log, TEXT("[RoleBehaviour] screenshot %d: %s"), Shots.Num(), Moment);
			}
		}
		int32 Problems = 0;
		void Check(bool bOk, const FString& What)
		{
			Problems += bOk ? 0 : 1;
			UE_LOG(LogFPSRL, Log, TEXT("[RoleBehaviour] %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
	};

	/** A direction around the player with a clear view out to Distance (the Lobby has pedestals). */
	static FVector ClearDirection(UWorld* World, const APawn* Player, float Distance)
	{
		for (const float Yaw : { 0.f, 90.f, -90.f, 180.f, 45.f, -45.f, 135.f, -135.f })
		{
			const FVector Direction = Player->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(Yaw, FVector::UpVector);
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Player->GetActorLocation() + Direction * Distance, Player->GetActorLocation(), ECC_Visibility,
				FCollisionQueryParams(TEXT("RoleBehaviour"), false, Player)))
			{
				return Direction;
			}
		}
		return Player->GetActorForwardVector().GetSafeNormal2D();
	}

	static ACharacter* SpawnRole(UWorld* World, APawn* Player, const TCHAR* Role, float Distance, bool bActive)
	{
		const UFPSRLEnemyDefinition* Def = LoadObject<UFPSRLEnemyDefinition>(nullptr, *FString::Printf(TEXT("/Game/MainProject/Contents/Data/Enemies/DA_Enemy_%s.DA_Enemy_%s"), Role, Role));
		UClass* Class = Def ? Def->EnemyClass.LoadSynchronous() : nullptr;
		if (!Class)
		{
			return nullptr;
		}
		const FVector Direction = ClearDirection(World, Player, Distance);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACharacter* Enemy = World->SpawnActor<ACharacter>(Class, FTransform((-Direction).Rotation(), Player->GetActorLocation() + Direction * Distance), Params);
		if (Enemy && !Enemy->GetController())
		{
			Enemy->SpawnDefaultController();
		}
		if (AFPSRLEnemyAIController* AI = Enemy ? Cast<AFPSRLEnemyAIController>(Enemy->GetController()) : nullptr)
		{
			AI->SetEncounterActive(bActive);
		}
		return Enemy;
	}

	/** A player bullet from in front of the player at the target (it flies and hits like a real shot). */
	static void Shoot(UWorld* World, APawn* Player, const AActor* Target)
	{
		UClass* Bullet = LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/BP_ShooterProjectile_Bullet.BP_ShooterProjectile_Bullet_C"));
		if (!Bullet)
		{
			return;
		}
		const FVector Direction = (Target->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal();
		const FTransform Shot(Direction.Rotation(), Player->GetActorLocation() + Direction * 90.f);
		if (AActor* Projectile = World->SpawnActorDeferred<AActor>(Bullet, Shot, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			AFPSRLProjectile::SetProjectileDamage(Projectile, 10.f);
			Projectile->FinishSpawning(Shot);
		}
	}
}

static FAutoConsoleCommandWithWorld GFPSRLRoleBehaviourTestCommand(TEXT("FPSRL.RoleBehaviourTest"),
	TEXT("Test (host, solo, Lobby): the Brute's roar / leap / shockwave / rush and the Skirmisher's sprint / phase ([RoleBehaviour])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		using namespace FPSRLRoleBehaviourTest;
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FState> S = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			ACharacter* Player = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			UFPSRLHealthComponent* PlayerHealth = Player ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!PlayerHealth || World->GetTimeSeconds() < 3.0)
			{
				return true;
			}
			const double Now = World->GetTimeSeconds();
			const double InStep = Now - S->StepTime;
			auto Next = [&S, Now](int32 Step) { S->Step = Step; S->StepTime = Now; };
			ACharacter* Enemy = S->Enemy.Get();
			AFPSRLEnemyAIController* AI = Enemy ? Cast<AFPSRLEnemyAIController>(Enemy->GetController()) : nullptr;
			if (Enemy && PC && S->Step != 2)
			{
				PC->SetControlRotation((Enemy->GetActorLocation() - Player->GetActorLocation()).Rotation() + FRotator(-5.f, 0.f, 0.f));
			}

			switch (S->Step)
			{
			// --- 1. Brute: roar, leap, shockwave, pause, rush -------------------------------------------------------
			case 0:
				PlayerHealth->Heal(10000.f);
				{
					const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
					FNavLocation OnNav;
					S->bNav = Nav && Nav->ProjectPointToNavigation(Player->GetActorLocation(), OnNav, FVector(200.f, 200.f, 300.f));
				}
				S->Enemy = SpawnRole(World, Player, TEXT("Brute"), 1300.f, true);
				S->Check(S->Enemy.IsValid(), TEXT("Brute spawned 13 m away"));
				S->Health = PlayerHealth->GetCurrentHealth();
				Next(S->Enemy.IsValid() ? 1 : 10);
				break;
			case 1:
				if (const UFPSRLEnemyAnimInstance* Anim = Enemy ? Cast<UFPSRLEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr)
				{
					S->bSawAction |= Anim->IsPlayingAction();
					if (InStep > 1.2 && AI && AI->GetAIState() == EFPSRLEnemyAIState::Attacking && Enemy->GetVelocity().IsNearlyZero())
					{
						S->Shot(PC, TEXT("Brute roars"));
					}
					if (Enemy->GetVelocity().Z > 0.f && Enemy->GetCharacterMovement()->IsFalling() && Enemy->GetActorLocation().Z > Player->GetActorLocation().Z + 150.f)
					{
						S->Shot(PC, TEXT("Brute mid-leap"));
					}
				}
				if (AI && AI->GetLeapsLanded() > 0)
				{
					S->LandedTime = Now;
					S->Check(S->bSawAction, TEXT("Brute: roar / leap animations played"));
					S->Check(FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()) < 400.f,
						FString::Printf(TEXT("Brute: landed by the player (%.1f m away)"), FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()) / 100.f));
					Next(2);
				}
				else if (InStep > 10.0)
				{
					S->Check(false, FString::Printf(TEXT("Brute: never leapt (%s)"), AI ? *AI->Describe() : TEXT("no AI")));
					Next(4);
				}
				break;
			case 2:
				// Before its first swing could land (1 s pause + 0.9 s wind-up): only the shockwave hurt.
				if (InStep > 0.15)
				{
					PC->SetControlRotation(FRotator(-40.f, PC->GetControlRotation().Yaw, 0.f));	// look down at the ring
					S->Shot(PC, TEXT("Brute landed: shockwave"));
				}
				if (InStep > 0.3 && InStep < 0.8)
				{
					S->SpeedAfterLanding = FMath::Max(S->SpeedAfterLanding, static_cast<float>(Enemy->GetVelocity().Size2D()));
				}
				if (InStep > 0.9)
				{
					S->Check(PlayerHealth->GetCurrentHealth() < S->Health, FString::Printf(TEXT("Brute: the landing shockwave hurt the player standing still (%.0f -> %.0f)"),
						S->Health, PlayerHealth->GetCurrentHealth()));
					S->Check(S->SpeedAfterLanding < 50.f, FString::Printf(TEXT("Brute: paused after landing (speed %.0f)"), S->SpeedAfterLanding));
					// Rush: move the player away so it has to run.
					const FVector Away = ClearDirection(World, Player, 1000.f);
					Player->TeleportTo(Player->GetActorLocation() + Away * 900.f, Player->GetActorRotation());
					Next(3);
				}
				break;
			case 3:
				S->RushSpeed = FMath::Max(S->RushSpeed, static_cast<float>(Enemy->GetVelocity().Size2D()));
				if (InStep > 2.5)
				{
					if (!S->bNav)
				{
					UE_LOG(LogFPSRL, Log, TEXT("[RoleBehaviour] skip Brute rush: no navmesh in this level (FPSRL.EnemyMotionWatch checks it in a real run)"));
				}
				else
				{
				S->Check(S->RushSpeed > 400.f && AI && AI->GetTarget() == Player, FString::Printf(TEXT("Brute: rushes the nearest player after the pause (speed %.0f, target %s)"),
						S->RushSpeed, AI ? *GetNameSafe(AI->GetTarget()) : TEXT("none")));
				}
					Next(4);
				}
				break;
			case 4:
				if (Enemy)
				{
					Enemy->Destroy();
				}
				Next(5);
				break;

			// --- 2. Shockwave: jump over it, then stand in it ---------------------------------------------------------
			case 5:
				PlayerHealth->Heal(10000.f);
				S->Enemy = SpawnRole(World, Player, TEXT("Brute"), 700.f, false);	// the source; its AI stays off
				if (!S->Enemy.IsValid())
				{
					Next(10);
					break;
				}
				S->Health = PlayerHealth->GetCurrentHealth();
				S->Wave = AFPSRLShockwave::Spawn(S->Enemy.Get(), S->Enemy->GetActorLocation() - FVector(0.f, 0.f, S->Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
					80.f, 900.f, 1000.f, 45.f);
				Next(6);
				break;
			case 6:
				if (InStep > 0.2 && Player->GetCharacterMovement()->IsMovingOnGround() && S->Wave.IsValid() && S->Wave->GetRadius() < 500.f)
				{
					Player->LaunchCharacter(FVector(0.f, 0.f, 650.f), false, true);	// jump as it arrives
				}
				if (InStep > 1.3)
				{
					S->Check(PlayerHealth->GetCurrentHealth() >= S->Health, FString::Printf(TEXT("Shockwave: jumping over it - no damage (%.0f -> %.0f)"),
						S->Health, PlayerHealth->GetCurrentHealth()));
					S->Wave = AFPSRLShockwave::Spawn(Enemy, Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
						80.f, 900.f, 1000.f, 45.f);
					Next(7);
				}
				break;
			case 7:
				if (InStep > 1.3)
				{
					S->Check(PlayerHealth->GetCurrentHealth() < S->Health, FString::Printf(TEXT("Shockwave: standing in it - hurt (%.0f -> %.0f)"),
						S->Health, PlayerHealth->GetCurrentHealth()));
					Enemy->Destroy();
					Next(10);
				}
				break;

			// --- 3. Skirmisher: sprint, phase on a bullet, bullets pass, melee lands ---------------------------------
			case 10:
				PlayerHealth->Heal(10000.f);
				S->Enemy = SpawnRole(World, Player, TEXT("Skirmisher"), 900.f, true);
				S->MaxSpeed = 0.f;
				Next(S->Enemy.IsValid() ? 11 : 99);
				break;
			case 11:
				S->MaxSpeed = FMath::Max(S->MaxSpeed, static_cast<float>(Enemy->GetVelocity().Size2D()));
				if (FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()) < 500.f || InStep > 3.0)
				{
					if (S->bNav)
					{
						S->Check(S->MaxSpeed > 650.f, FString::Printf(TEXT("Skirmisher: sprints in (top speed %.0f)"), S->MaxSpeed));
					}
					S->Health = Enemy->FindComponentByClass<UFPSRLHealthComponent>()->GetCurrentHealth();
					Shoot(World, Player, Enemy);
					Next(12);
				}
				break;
			case 12:
			{
				const UFPSRLHealthComponent* Health = Enemy->FindComponentByClass<UFPSRLHealthComponent>();
				const UFPSRLEnemyRoleComponent* RoleView = Enemy->FindComponentByClass<UFPSRLEnemyRoleComponent>();
				if (AI && AI->GetPhases() > 0)
				{
					S->Check(Health->GetCurrentHealth() < S->Health, FString::Printf(TEXT("Skirmisher: the first bullet hurt it (%.0f -> %.0f)"), S->Health, Health->GetCurrentHealth()));
					S->Check(RoleView && RoleView->IsPhased() && Health->IsPhased(), TEXT("Skirmisher: phased after the bullet"));
					S->PhaseStart = Now;
					S->Health = Health->GetCurrentHealth();
					Next(13);
				}
				else if (InStep > 2.0)
				{
					S->Check(false, FString::Printf(TEXT("Skirmisher: never phased (health %.0f -> %.0f)"), S->Health, Health->GetCurrentHealth()));
					Next(15);
				}
				break;
			}
			case 13:
				if (InStep > 0.2)
				{
					S->Shot(PC, TEXT("Skirmisher phased"));
				}
				if (InStep > 0.45 && InStep < 0.5)
				{
					Shoot(World, Player, Enemy);	// while phased
				}
				if (InStep > 0.95)
				{
					const UFPSRLHealthComponent* Health = Enemy->FindComponentByClass<UFPSRLHealthComponent>();
					S->Check(FMath::IsNearlyEqual(Health->GetCurrentHealth(), S->Health), FString::Printf(TEXT("Skirmisher: a bullet while phased does nothing (%.0f -> %.0f)"),
						S->Health, Health->GetCurrentHealth()));
					// Melee while phased: a swing aimed at it.
					const FVector ToEnemy = Enemy->GetActorLocation() - Player->GetActorLocation();
					Player->SetActorRotation(ToEnemy.GetSafeNormal2D().Rotation());
					FPSRLCombat::MeleeSweep(Player, PC, ToEnemy.Size2D() + 100.f, 100.f, 15.f, 1);
					S->Check(Health->GetCurrentHealth() < S->Health, FString::Printf(TEXT("Skirmisher: melee while phased still hurts (%.0f -> %.0f, phased %d)"),
						S->Health, Health->GetCurrentHealth(), Health->IsPhased() ? 1 : 0));
					Next(14);
				}
				break;
			case 14:
			{
				const UFPSRLEnemyRoleComponent* RoleView = Enemy->FindComponentByClass<UFPSRLEnemyRoleComponent>();
				const float PhaseSeconds = AI && AI->GetProfile() ? AI->GetProfile()->PhaseSeconds : 0.f;
				const double Phased = Now - S->PhaseStart;
				if ((RoleView && !RoleView->IsPhased()) || Phased > PhaseSeconds + 1.5)
				{
					S->Check(RoleView && !RoleView->IsPhased() && FMath::Abs(Phased - PhaseSeconds) < 0.5,
						FString::Printf(TEXT("Skirmisher: phased back in after %.1f s (profile %.1f s)"), Phased, PhaseSeconds));
					Next(15);
				}
			}
				break;
			case 15:
				if (Enemy)
				{
					Enemy->Destroy();
				}
				Next(99);
				break;
			default:
				UE_LOG(LogFPSRL, Log, TEXT("[RoleBehaviour] done: %d problem(s)"), S->Problems);
				return false;
			}
			return true;
		}));
	}));
#endif

#if !UE_BUILD_SHIPPING
// Test only: FPSRL.EnemyMotionWatch [seconds] (host, during a real run, e.g. Lobby + fpsrl.Enemy.ForceRole Brute +
// fpsrl.Autopilot.FightSeconds 30 + FPSRLAutoRun). Each role enemy's top speed, time spent moving, leaps, phases and
// actions over the window ([Motion]) - the movement the Lobby (no navmesh) can't show.
static FAutoConsoleCommandWithWorldAndArgs GFPSRLEnemyMotionWatchCommand(TEXT("FPSRL.EnemyMotionWatch"),
	TEXT("Test (host, real run): log the role enemies' speeds, leaps and phases once their encounter starts ([Motion])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		struct FWatch
		{
			float Seconds = 30.f;
			double Start = 0.0;
			double NextSample = 0.0;
			TMap<FString, float> TopSpeed;
			TMap<FString, int32> MovingSamples;
			TMap<FString, int32> Samples;
			bool bShots = false;
			bool bStrafe = false;
			float HostTopSpeed = 0.f;
			int32 ShotsTaken = 0;
			double NextShot = 0.0;
		};
		TSharedRef<FWatch> W = MakeShared<FWatch>();
		W->Seconds = Args.IsEmpty() ? 30.f : FCString::Atof(*Args[0]);
		W->bShots = Args.Contains(TEXT("shots"));
		W->bStrafe = Args.Contains(TEXT("strafe"));	// the host keeps walking in circles (melee must still land)
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, W](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			if (!World)
			{
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (W->bStrafe && W->Start > 0.0)
			{
				if (APawn* Host = GI->GetFirstLocalPlayerController(World) ? GI->GetFirstLocalPlayerController(World)->GetPawn() : nullptr)
				{
					Host->AddMovementInput(FRotator(0.f, static_cast<float>(FMath::Fmod((Now - W->Start) * 70.0, 360.0)), 0.f).Vector());
					W->HostTopSpeed = FMath::Max(W->HostTopSpeed, static_cast<float>(Host->GetVelocity().Size2D()));
				}
			}
			if (Now < W->NextSample)
			{
				return true;
			}
			W->NextSample = Now + 0.25;
			int32 Active = 0;
			int32 Leaps = 0;
			int32 Phases = 0;
			for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
			{
				const APawn* Pawn = It->GetPawn();
				if (!Pawn || It->GetAIState() == EFPSRLEnemyAIState::Inactive)
				{
					continue;
				}
				++Active;
				Leaps += It->GetLeapsLanded();
				Phases += It->GetPhases();
				const FString Class = Pawn->GetClass()->GetName();
				const float Speed = Pawn->GetVelocity().Size2D();
				W->TopSpeed.FindOrAdd(Class) = FMath::Max(W->TopSpeed.FindRef(Class), Speed);
				W->MovingSamples.FindOrAdd(Class) += Speed > 100.f ? 1 : 0;
				W->Samples.FindOrAdd(Class) += 1;
			}
			if (Active == 0)
			{
				return true;	// until an encounter runs
			}
			// Rendered runs ("shots"): every 2 s the host looks at the nearest moving role enemy and takes a screenshot (6 at most).
			APlayerController* PC = GI->GetFirstLocalPlayerController(World);
			if (W->bShots && PC && PC->GetPawn() && W->ShotsTaken < 6 && Now >= W->NextShot)
			{
				const APawn* Nearest = nullptr;
				for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
				{
					const APawn* Pawn = It->GetPawn();
					if (Pawn && Pawn->GetVelocity().Size2D() > 200.f && (!Nearest || FVector::Dist(Pawn->GetActorLocation(), PC->GetPawn()->GetActorLocation()) < FVector::Dist(Nearest->GetActorLocation(), PC->GetPawn()->GetActorLocation())))
					{
						Nearest = Pawn;
					}
				}
				if (Nearest)
				{
					PC->SetControlRotation((Nearest->GetActorLocation() - PC->GetPawn()->GetActorLocation()).Rotation());
					PC->ConsoleCommand(TEXT("Shot"));
					++W->ShotsTaken;
					W->NextShot = Now + 2.0;
					UE_LOG(LogFPSRL, Log, TEXT("[Motion] screenshot of %s at %.0f cm/s"), *Nearest->GetName(), Nearest->GetVelocity().Size2D());
				}
			}
			W->Start = W->Start > 0.0 ? W->Start : Now;
			if (Now - W->Start < W->Seconds)
			{
				return true;
			}
			for (const TPair<FString, float>& Entry : W->TopSpeed)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[Motion] %s: top speed %.0f cm/s, moving %.0f%% of the time"), *Entry.Key, Entry.Value,
					100.f * W->MovingSamples.FindRef(Entry.Key) / FMath::Max(1, W->Samples.FindRef(Entry.Key)));
			}
			int32 MeleeHits = 0;
			int32 Attacks = 0;
			for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
			{
				MeleeHits += It->GetMeleeHits();
				Attacks += It->GetAttacksExecuted();
			}
			UE_LOG(LogFPSRL, Log, TEXT("[Motion] attacks %d, melee hits %d (enemies alive now); host top speed %.0f"), Attacks, MeleeHits, W->HostTopSpeed);
			UE_LOG(LogFPSRL, Log, TEXT("[Motion] done: %d leap(s) landed, %d phase(s) (enemies alive now)"), Leaps, Phases);
			return false;
		}));
	}));
#endif
