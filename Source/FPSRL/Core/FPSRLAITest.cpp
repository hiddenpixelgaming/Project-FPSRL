// Fill out your copyright notice in the Description page of Project Settings.

// Test only: the enemy AI foundation (in-memory test profiles, never assets).
//  FPSRL.AITest            (host, solo, Lobby) target selection strategies; Inactive until its encounter starts; alerted
//                          targeting and shooting; no shots through a wall; damage interrupt; stun; melee; leash; aggro on
//                          damage; death. Movement and navigation are checked in real rooms (fpsrl.Autopilot.FightSeconds).
//  FPSRL.AIMultiTest N     (host with N players) each enemy picks its own target; retargets the attacker; drops a dead
//                          target for another player.

#include "AI/FPSRLEnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Data/FPSRLEnemyBehaviorProfile.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Types/FPSRLGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLAITest
{
	struct FReport
	{
		int32 Problems = 0;
		void Check(bool bOk, const FString& What)
		{
			Problems += bOk ? 0 : 1;
			UE_LOG(LogFPSRL, Log, TEXT("[AITest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
		}
	};

	UFPSRLEnemyBehaviorProfile* MakeProfile(const TCHAR* Name, EFPSRLEnemyAttackAction Action)
	{
		UFPSRLEnemyBehaviorProfile* Profile = NewObject<UFPSRLEnemyBehaviorProfile>(GetTransientPackage(), Name);
		Profile->MovementStyle = EFPSRLMovementStyle::MaintainDistance;
		Profile->PreferredMinDistance = 0.f;
		Profile->PreferredMaxDistance = 2500.f;
		Profile->ReactionTime = 0.2f;
		Profile->ThinkInterval = 0.1f;
		FFPSRLEnemyAttack& Attack = Profile->Attacks.AddDefaulted_GetRef();
		Attack.Action = Action;
		Attack.MaxRange = Action == EFPSRLEnemyAttackAction::Melee ? 250.f : 2500.f;
		Attack.MaxAngle = Action == EFPSRLEnemyAttackAction::Melee ? 60.f : 30.f;
		Attack.WindupSeconds = 0.2f;
		Attack.ExecuteSeconds = Action == EFPSRLEnemyAttackAction::Melee ? 0.1f : 0.6f;
		Attack.RecoverySeconds = 0.2f;
		Attack.Cooldown = 0.5f;
		Attack.MeleeDamage = 15.f;
		return Profile;
	}

	void TestSelection(FReport& Report)
	{
		using namespace FPSRLEnemyAI;
		AActor* A = reinterpret_cast<AActor*>(1);	// identities only
		AActor* B = reinterpret_cast<AActor*>(2);
		AActor* C = reinterpret_cast<AActor*>(3);
		TArray<FTargetCandidate> Candidates;
		Candidates.Add({ A, 400.f, 0.9f, 0.f, false, false });
		Candidates.Add({ B, 900.f, 0.2f, 50.f, true, false });
		Candidates.Add({ C, 1500.f, 0.6f, 120.f, false, true });
		auto Pick = [&Candidates](EFPSRLTargetSelection Strategy, float Preferred = 1000.f) { const int32 I = SelectTarget(Strategy, Candidates, Preferred); return I == INDEX_NONE ? nullptr : Candidates[I].Actor; };
		Report.Check(Pick(EFPSRLTargetSelection::ClosestPlayer) == A, TEXT("strategy ClosestPlayer picks the 400 cm player"));
		Report.Check(Pick(EFPSRLTargetSelection::LastDamagingPlayer) == B, TEXT("strategy LastDamagingPlayer picks whoever hit it last"));
		Report.Check(Pick(EFPSRLTargetSelection::HighestThreat) == C, TEXT("strategy HighestThreat picks the most damage (threat 120)"));
		Report.Check(Pick(EFPSRLTargetSelection::LowestHealthPlayer) == B, TEXT("strategy LowestHealthPlayer picks the 20% health player"));
		Report.Check(Pick(EFPSRLTargetSelection::PreferredRangePlayer, 1000.f) == B, TEXT("strategy PreferredRangePlayer picks the one nearest 1000 cm"));
		Report.Check(Pick(EFPSRLTargetSelection::CurrentTargetUntilInvalid) == C, TEXT("strategy CurrentTargetUntilInvalid keeps its current target"));
		FRandomStream Random(7);
		TSet<AActor*> Seen;
		for (int32 Roll = 0; Roll < 60; ++Roll)
		{
			Seen.Add(Candidates[SelectTarget(EFPSRLTargetSelection::RandomValidPlayer, Candidates, 0.f, &Random)].Actor);
		}
		Report.Check(Seen.Num() == 3, FString::Printf(TEXT("strategy RandomValidPlayer spreads over all valid players (%d of 3 in 60 picks)"), Seen.Num()));
		TArray<FTargetCandidate> NoThreat = Candidates;
		for (FTargetCandidate& Candidate : NoThreat)
		{
			Candidate.Threat = 0.f;
			Candidate.bLastDamager = false;
			Candidate.bCurrent = false;
		}
		Report.Check(NoThreat[SelectTarget(EFPSRLTargetSelection::HighestThreat, NoThreat, 0.f)].Actor == A && NoThreat[SelectTarget(EFPSRLTargetSelection::LastDamagingPlayer, NoThreat, 0.f)].Actor == A
			&& NoThreat[SelectTarget(EFPSRLTargetSelection::CurrentTargetUntilInvalid, NoThreat, 0.f)].Actor == A,
			TEXT("threat / damager / current strategies fall back to the closest player when nobody qualifies"));
		Report.Check(SelectTarget(EFPSRLTargetSelection::ClosestPlayer, {}, 0.f) == INDEX_NONE, TEXT("no valid player: no target"));
	}

	AFPSRLEnemyAIController* NewestAI(UWorld* World, const TSet<AFPSRLEnemyAIController*>& Known)
	{
		for (TActorIterator<AFPSRLEnemyAIController> It(World); It; ++It)
		{
			if (!Known.Contains(*It) && It->GetPawn())
			{
				return *It;
			}
		}
		return nullptr;
	}

	struct FLive
	{
		FReport Report;
		int32 Step = 0;
		TSet<AFPSRLEnemyAIController*> Known;
		TWeakObjectPtr<AFPSRLEnemyAIController> Ranged, Melee, Aggro;
		TWeakObjectPtr<AActor> Wall;
		TArray<TStrongObjectPtr<UFPSRLEnemyBehaviorProfile>> Profiles;
		float HealthMark = 0.f;
		int32 AttackMark = 0;
		UFPSRLEnemyBehaviorProfile* Keep(UFPSRLEnemyBehaviorProfile* Profile) { Profiles.Emplace(Profile); return Profile; }
	};
}

static FAutoConsoleCommandWithWorld GFPSRLAITestCommand(TEXT("FPSRL.AITest"),
	TEXT("Host test: enemy AI foundation ([AITest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLAITest::FLive> Live = MakeShared<FPSRLAITest::FLive>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Live](float)
		{
			using namespace FPSRLAITest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* PlayerHealth = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!PlayerHealth || !World->GetGameState())
			{
				return ++Live->Step < 400;
			}
			FReport& Report = Live->Report;
			auto State = [](const AFPSRLEnemyAIController* AI) { return AI ? AI->GetAIState() : EFPSRLEnemyAIState::Dead; };
			auto Name = [](const AFPSRLEnemyAIController* AI) { return AI ? AI->Describe() : FString(TEXT("gone")); };
			auto Spawn = [PC](const TCHAR* Distance, const TCHAR* Side) { PC->ServerTestCommand(TEXT("SpawnTestAI"), Distance, Side); };

			++Live->Step;
			AFPSRLEnemyAIController* Ranged = Live->Ranged.Get();
			AFPSRLEnemyAIController* Melee = Live->Melee.Get();
			AFPSRLEnemyAIController* Aggro = Live->Aggro.Get();
			if (Live->Step < 1000)
			{
				Live->Step = 1000;
				TestSelection(Report);
				Spawn(TEXT("-1000"), TEXT("0"));	// behind the player: a clear line (the Lobby's weapon station trigger stops bullets)
				return true;
			}
			switch (Live->Step)
			{
			case 1001:
				Live->Ranged = NewestAI(World, Live->Known);
				Live->Known.Add(Live->Ranged.Get());
				break;
			case 1012:
			{
				// Encounter activation: nothing before it.
				Report.Check(Ranged && State(Ranged) == EFPSRLEnemyAIState::Inactive && !Ranged->GetTarget() && Ranged->GetAttacksExecuted() == 0,
					FString::Printf(TEXT("before its encounter starts, 1 s with a player in plain view: %s (expect Inactive, no target, no attacks)"), *Name(Ranged)));
				if (Ranged)
				{
					Ranged->SetProfile(Live->Keep(MakeProfile(TEXT("TEST_AI_Ranged"), EFPSRLEnemyAttackAction::FireWeapon)));
					if (UFPSRLHealthComponent* EnemyHealth = Ranged->GetPawn()->FindComponentByClass<UFPSRLHealthComponent>())
					{
						EnemyHealth->OutgoingDamageMultiplier = 0.2f;	// 10 per shot: the player must survive to the melee checks
					}
					Live->HealthMark = PlayerHealth->GetCurrentHealth();
					Ranged->SetEncounterActive(true);
				}
				break;
			}
			case 1030:
			{
				const float Taken = Live->HealthMark - PlayerHealth->GetCurrentHealth();
				Report.Check(Ranged && Ranged->GetTarget() == Pawn && Ranged->GetAttacksExecuted() > 0 && Taken > 0.f,
					FString::Printf(TEXT("encounter started: %s; player took %.0f (expect it targets the player, attacks and hits)"), *Name(Ranged), Taken));
				// A wall between them.
				if (Ranged)
				{
					const FVector Middle = (Ranged->GetPawn()->GetActorLocation() + Pawn->GetActorLocation()) * 0.5f;
					AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(Middle, (Pawn->GetActorLocation() - Middle).Rotation());
					Block->SetMobility(EComponentMobility::Movable);
					Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					Block->SetActorScale3D(FVector(0.3f, 6.f, 6.f));
					Live->Wall = Block;
				}
				break;
			}
			case 1036:
				Live->AttackMark = Ranged ? Ranged->GetAttacksExecuted() : 0;
				Live->HealthMark = PlayerHealth->GetCurrentHealth();
				break;
			case 1060:
			{
				const float Taken = Live->HealthMark - PlayerHealth->GetCurrentHealth();
				Report.Check(Ranged && !Ranged->HasLineOfSightTo(Pawn) && Ranged->GetAttacksExecuted() == Live->AttackMark && Taken <= 0.f,
					FString::Printf(TEXT("wall between them for 2.4 s: %s; new attacks %d, player took %.0f (expect no sight, no attacks, no damage)"),
						*Name(Ranged), Ranged ? Ranged->GetAttacksExecuted() - Live->AttackMark : -1, Taken));
				if (AActor* Block = Live->Wall.Get())
				{
					Block->Destroy();
				}
				Live->AttackMark = Ranged ? Ranged->GetAttacksExecuted() : 0;
				break;
			}
			case 1080:
			{
				Report.Check(Ranged && Ranged->GetAttacksExecuted() > Live->AttackMark, FString::Printf(TEXT("wall removed: %s (expect attacks again)"), *Name(Ranged)));
				// Damage response: an interruptible, long attack is cancelled by a hit.
				Live->AttackMark = 0;
				if (Ranged)
				{
					UFPSRLEnemyBehaviorProfile* Interruptible = Live->Keep(MakeProfile(TEXT("TEST_AI_Interruptible"), EFPSRLEnemyAttackAction::FireWeapon));
					Interruptible->DamageResponse = EFPSRLDamageResponse::Interrupt;
					Interruptible->Attacks[0].bInterruptible = true;
					Interruptible->Attacks[0].ExecuteSeconds = 4.f;
					Ranged->SetProfile(Interruptible);
				}
				break;
			}
			case 1090:
			{
				const bool bWasFiring = Ranged && Ranged->GetAIState() == EFPSRLEnemyAIState::Attacking && Ranged->WantsToKeepFiring();
				if (!bWasFiring && Ranged && ++Live->AttackMark < 60)
				{
					--Live->Step;	// wait for a burst to be under way
					break;
				}
				if (Ranged)
				{
					UGameplayStatics::ApplyDamage(Ranged->GetPawn(), 1.f, PC, Pawn, nullptr);
				}
				Report.Check(bWasFiring && Ranged && Ranged->GetAIState() == EFPSRLEnemyAIState::Recovering && !Ranged->WantsToKeepFiring(),
					FString::Printf(TEXT("hit during a 4 s interruptible burst (was firing: %s): %s (expect Recovering, trigger released)"), bWasFiring ? TEXT("yes") : TEXT("no"), *Name(Ranged)));
				// Stun.
				if (UAbilitySystemComponent* ASC = Ranged ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Ranged->GetPawn()) : nullptr)
				{
					ASC->AddLooseGameplayTag(FPSRLGameplayTags::Status_Stunned);
				}
				Live->AttackMark = Ranged ? Ranged->GetAttacksExecuted() : 0;
				break;
			}
			case 1110:
			{
				Report.Check(Ranged && Ranged->GetAIState() == EFPSRLEnemyAIState::Disabled && Ranged->GetAttacksExecuted() == Live->AttackMark,
					FString::Printf(TEXT("stunned 2 s: %s (expect Disabled, no attacks)"), *Name(Ranged)));
				if (UAbilitySystemComponent* ASC = Ranged ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Ranged->GetPawn()) : nullptr)
				{
					ASC->RemoveLooseGameplayTag(FPSRLGameplayTags::Status_Stunned);
				}
				break;
			}
			case 1112:
				Report.Check(Ranged && Ranged->GetAIState() != EFPSRLEnemyAIState::Disabled, FString::Printf(TEXT("stun removed: %s (expect active again)"), *Name(Ranged)));
				if (Ranged)
				{
					Ranged->SetEncounterActive(false);	// out of the way for the melee checks
					if (UFPSRLHealthComponent* EnemyHealth = Ranged->GetPawn()->FindComponentByClass<UFPSRLHealthComponent>())
					{
						EnemyHealth->OutgoingDamageMultiplier = 0.f;	// its bullets still in flight must not count as melee damage
					}
				}
				Spawn(TEXT("150"), TEXT("0"));
				break;
			case 1113:
				Report.Check(Ranged && Ranged->GetAIState() == EFPSRLEnemyAIState::Inactive && !Ranged->GetTarget(), FString::Printf(TEXT("encounter reset: %s (expect Inactive)"), *Name(Ranged)));
				Live->Melee = NewestAI(World, Live->Known);
				Live->Known.Add(Live->Melee.Get());
				if (AFPSRLEnemyAIController* NewMelee = Live->Melee.Get())
				{
					NewMelee->SetProfile(Live->Keep(MakeProfile(TEXT("TEST_AI_Melee"), EFPSRLEnemyAttackAction::Melee)));
					Live->HealthMark = PlayerHealth->GetCurrentHealth();
					NewMelee->SetEncounterActive(true);
				}
				break;
			case 1133:
			{
				const float Taken = Live->HealthMark - PlayerHealth->GetCurrentHealth();
				Report.Check(Melee && Melee->GetAttacksExecuted() > 0 && Taken > 0.f && FMath::IsNearlyEqual(FMath::Fmod(Taken, 15.f), 0.f, 0.01f),
					FString::Printf(TEXT("melee archetype 1.5 m away: %s; player took %.0f (expect swings of 15)"), *Name(Melee), Taken));
				// Leash: a target beyond MaxTargetDistance is dropped.
				if (Melee)
				{
					UFPSRLEnemyBehaviorProfile* Leashed = Live->Keep(MakeProfile(TEXT("TEST_AI_Leashed"), EFPSRLEnemyAttackAction::Melee));
					Leashed->MaxTargetDistance = 50.f;
					Leashed->bAlertedOnEncounterStart = false;
					Melee->SetProfile(Leashed);
				}
				break;
			}
			case 1138:
				Report.Check(Melee && !Melee->GetTarget(), FString::Printf(TEXT("player beyond its 50 cm leash: %s (expect the target dropped)"), *Name(Melee)));
				if (Melee)
				{
					if (UFPSRLHealthComponent* EnemyHealth = Melee->GetPawn()->FindComponentByClass<UFPSRLHealthComponent>())
					{
						EnemyHealth->Kill();
					}
				}
				// Aggro on damage: an enemy looking away that can't detect anyone.
				Spawn(TEXT("1200"), TEXT("1500"));
				break;
			case 1139:
				Live->Aggro = NewestAI(World, Live->Known);
				Live->Known.Add(Live->Aggro.Get());
				if (AFPSRLEnemyAIController* NewAggro = Live->Aggro.Get())
				{
					UFPSRLEnemyBehaviorProfile* Blind = Live->Keep(MakeProfile(TEXT("TEST_AI_Blind"), EFPSRLEnemyAttackAction::FireWeapon));
					Blind->bAlertedOnEncounterStart = false;
					Blind->DetectionAngle = 10.f;
					Blind->ProximityAggroRadius = 0.f;
					NewAggro->SetProfile(Blind);
					NewAggro->SetEncounterActive(true);
				}
				break;
			case 1149:
			{
				Report.Check(Melee && Melee->GetAIState() == EFPSRLEnemyAIState::Dead, FString::Printf(TEXT("killed: %s (expect Dead)"), *Name(Melee)));
				Report.Check(Aggro && !Aggro->GetTarget(), FString::Printf(TEXT("player outside its 10 degree view: %s (expect no target)"), *Name(Aggro)));
				if (Aggro)
				{
					UGameplayStatics::ApplyDamage(Aggro->GetPawn(), 1.f, PC, Pawn, nullptr);
				}
				break;
			}
			case 1151:
				Report.Check(Aggro && Aggro->GetTarget() == Pawn, FString::Printf(TEXT("then shot by the player: %s (expect it targets the attacker)"), *Name(Aggro)));
				UE_LOG(LogFPSRL, Log, TEXT("[AITest] done: %d problem(s)"), Report.Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			default:
				break;
			}
			return true;
		}), 0.1f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRLAIMultiTestCommand(TEXT("FPSRL.AIMultiTest"),
	TEXT("Host test: with N players, each enemy picks its own target, retargets its attacker and drops a dead target ([AITest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		const int32 Wanted = Args.IsEmpty() ? 1 : FMath::Max(1, FCString::Atoi(*Args[0]));
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<int32> Problems = MakeShared<int32>(0);
		TSharedRef<TArray<TWeakObjectPtr<AFPSRLEnemyAIController>>> Enemies = MakeShared<TArray<TWeakObjectPtr<AFPSRLEnemyAIController>>>();
		TSharedRef<TArray<TWeakObjectPtr<APawn>>> Players = MakeShared<TArray<TWeakObjectPtr<APawn>>>();
		TSharedRef<TStrongObjectPtr<UFPSRLEnemyBehaviorProfile>> Profile = MakeShared<TStrongObjectPtr<UFPSRLEnemyBehaviorProfile>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Problems, Enemies, Players, Profile, Wanted](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* HostPC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			auto Check = [Problems](bool bOk, const FString& What)
			{
				*Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[AITest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto Finish = [HostPC, Problems, Wanted]()
			{
				UE_LOG(LogFPSRL, Log, TEXT("[AITest] done: %d problem(s) with %d player(s)"), *Problems, Wanted);
				if (HostPC)
				{
					HostPC->ConsoleCommand(TEXT("quit"));
				}
				return false;
			};
			++*Step;
			if (*Step < 1000)
			{
				TArray<APawn*> Found;
				for (APlayerState* Player : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
				{
					if (Player && Player->GetPawn())
					{
						Found.Add(Player->GetPawn());
					}
				}
				if (!HostPC || !HostPC->GetPawn() || Found.Num() < Wanted)
				{
					if (*Step > 900)
					{
						Check(false, FString::Printf(TEXT("only %d of %d players joined"), Found.Num(), Wanted));
						return Finish();
					}
					return true;
				}
				*Step = 1000;
				// Line the players up 800 cm apart and put one enemy 250 cm in front of each (the host decides everything).
				const FVector Origin = HostPC->GetPawn()->GetActorLocation();
				const FVector Right = HostPC->GetPawn()->GetActorRightVector();
				const FVector Forward = HostPC->GetPawn()->GetActorForwardVector();
				UClass* EnemyClass = LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
				UFPSRLEnemyBehaviorProfile* Shared = FPSRLAITest::MakeProfile(TEXT("TEST_AI_Multi"), EFPSRLEnemyAttackAction::FireWeapon);
				Shared->DamageResponse = EFPSRLDamageResponse::RetargetAttacker;
				Shared->RetargetInterval = 60.f;	// only the events under test change targets
				Shared->Attacks[0].WindupSeconds = 30.f;	// aims but never shoots (players must not die mid-test)
				Profile->Reset(Shared);
				for (int32 Index = 0; Index < Found.Num(); ++Index)
				{
					const FVector Spot = Origin + Right * (800.f * Index);
					Found[Index]->TeleportTo(Spot, Found[Index]->GetActorRotation(), false, true);
					Players->Add(Found[Index]);
					const FTransform At((-Forward).Rotation(), Spot + Forward * 250.f);
					if (APawn* Enemy = World->SpawnActor<APawn>(EnemyClass, At, FActorSpawnParameters()))
					{
						Enemies->Add(Cast<AFPSRLEnemyAIController>(Enemy->GetController()));
					}
				}
				return true;
			}
			switch (*Step)
			{
			case 1004:
				for (const TWeakObjectPtr<AFPSRLEnemyAIController>& AI : *Enemies)
				{
					if (AI.IsValid())
					{
						AI->SetProfile(Profile->Get());
						AI->SetEncounterActive(true);
					}
				}
				break;
			case 1020:
			{
				for (int32 Index = 0; Index < Enemies->Num(); ++Index)
				{
					const AFPSRLEnemyAIController* AI = (*Enemies)[Index].Get();
					Check(AI && AI->GetTarget() == (*Players)[Index].Get(),
						FString::Printf(TEXT("enemy %d of %d: %s (expect player %d, its closest)"), Index + 1, Enemies->Num(), AI ? *AI->Describe() : TEXT("missing"), Index + 1));
				}
				if (Wanted > 1 && (*Enemies)[0].IsValid())
				{
					// The last player shoots enemy 1 from afar: it switches to them.
					APawn* Shooter = (*Players)[Wanted - 1].Get();
					UGameplayStatics::ApplyDamage((*Enemies)[0]->GetPawn(), 1.f, Shooter->GetController(), Shooter, nullptr);
				}
				break;
			}
			case 1022:
				if (Wanted > 1)
				{
					const AFPSRLEnemyAIController* AI = (*Enemies)[0].Get();
					Check(AI && AI->GetTarget() == (*Players)[Wanted - 1].Get(), FString::Printf(TEXT("enemy 1 shot by player %d: %s (expect it retargets the attacker)"), Wanted, AI ? *AI->Describe() : TEXT("missing")));
					// Player 2 dies: whoever targeted them must pick someone else.
					if (UFPSRLHealthComponent* Health = (*Players)[1].IsValid() ? (*Players)[1]->FindComponentByClass<UFPSRLHealthComponent>() : nullptr)
					{
						Health->ApplyEnvironmentDamage(100000.f);
					}
				}
				break;
			case 1030:
				if (Wanted > 1)
				{
					const AFPSRLEnemyAIController* AI = (*Enemies)[1].Get();
					const AActor* NewTarget = AI ? AI->GetTarget() : nullptr;
					Check(AI && NewTarget && NewTarget != (*Players)[1].Get(),
						FString::Printf(TEXT("player 2 died: enemy 2 now %s (expect another living player)"), AI ? *AI->Describe() : TEXT("missing")));
				}
				return Finish();
			default:
				break;
			}
			return true;
		}), 0.1f);
	}));
#endif
