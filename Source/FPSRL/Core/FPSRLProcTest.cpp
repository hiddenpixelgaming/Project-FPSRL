// Fill out your copyright notice in the Description page of Project Settings.

// Test only: the Blessing proc framework.
//  FPSRL.ProcTest       (host, solo) seeded simulations of every proc model (same proc at different fire rates, shotgun
//                       attack vs pellet, melee speeds, cooldown, accumulation) and live checks with real weapons: a
//                       shotgun blast, melee swings, a fire-rate / melee-speed upgrade changing the chance, a periodic proc.
//  FPSRL.ProcMultiTest N (host with N players) every player owns the same Blessing; each one's hits only move their own
//                       proc state.
// The test Blessings are in-memory only (never assets, never in the pool).

#include "Abilities/Effects/FPSRLHealthEffects.h"
#include "Abilities/Effects/FPSRLStatusEffects.h"
#include "AbilitySystemComponent.h"
#include "Combat/FPSRLProcRules.h"
#include "Combat/FPSRLWeapon.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBlessingEffects.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Types/FPSRLGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLProcTest
{
	struct FReport
	{
		int32 Problems = 0;
		void Check(bool bOk, const FString& What)
		{
			Problems += bOk ? 0 : 1;
			UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
		}
	};

	FFPSRLBlessingTrigger MakeTrigger(EFPSRLProcModel Model, FGameplayTag Event)
	{
		FFPSRLBlessingTrigger Trigger;
		Trigger.Model = Model;
		Trigger.Event = Event;
		return Trigger;
	}

	struct FSimResult
	{
		int32 Procs = 0;
		int32 Evaluations = 0;
		double Seconds = 0.0;
		float ProcsPerSecond() const { return Seconds > 0.0 ? static_cast<float>(Procs / Seconds) : 0.f; }
	};

	/** Attacks every Interval seconds for Seconds; each attack produces HitsPerAttack events (pellets / cleave targets). */
	FSimResult Simulate(const FFPSRLBlessingTrigger& Trigger, float Interval, double Seconds, int32 HitsPerAttack, float Damage, int32 Seed)
	{
		FRandomStream Random(Seed);
		FPSRLProcs::FProcState State;
		FSimResult Result;
		Result.Seconds = Seconds;
		int32 AttackId = 0;
		const bool bPerHit = !Trigger.Event.MatchesTagExact(FPSRLGameplayTags::Event_Attack) && Trigger.Scope == EFPSRLProcScope::EachEvent;
		for (double Now = 0.0; Now < Seconds - KINDA_SMALL_NUMBER; Now += Interval)
		{
			++AttackId;
			for (int32 Hit = 0; Hit < HitsPerAttack; ++Hit)
			{
				FPSRLProcs::FProcEvent Event;
				Event.Now = Now;
				Event.AttackId = AttackId;
				Event.Damage = Damage;
				Event.EventInterval = bPerHit ? Interval / HitsPerAttack : Interval;	// as the component computes it
				Event.Random = &Random;
				Result.Procs += FPSRLProcs::Evaluate(Trigger, State, Event) ? 1 : 0;
			}
		}
		Result.Evaluations = State.Evaluations;
		return Result;
	}

	void RunSimulations(FReport& Report)
	{
		const FGameplayTag Hit = FPSRLGameplayTags::Event_Hit;
		const FGameplayTag Attack = FPSRLGameplayTags::Event_Attack;

		// Same normalized proc (1 per second), a 1 attack/s weapon and a 10 attacks/s weapon.
		FFPSRLBlessingTrigger Normalized = MakeTrigger(EFPSRLProcModel::Normalized, Attack);
		Normalized.ProcsPerSecond = 1.f;
		for (const float Interval : { 1.f, 0.1f })
		{
			const FSimResult R = Simulate(Normalized, Interval, 20000.0, 1, 0.f, 11);
			Report.Check(FMath::Abs(R.ProcsPerSecond() - 1.f) < 0.05f, FString::Printf(TEXT("normalized 1/s, weapon at %.0f attacks/s: %.3f procs/s, chance per attack %.2f (expect ~1.00 procs/s)"),
				1.f / Interval, R.ProcsPerSecond(), FPSRLProcs::GetChance(Normalized, [&] { FPSRLProcs::FProcEvent E; E.EventInterval = Interval; return E; }())));
		}

		// Chance per event (Abyssus-style "X% chance on hit"): a faster weapon procs proportionally more.
		FFPSRLBlessingTrigger PerHit = MakeTrigger(EFPSRLProcModel::ChancePerEvent, Hit);
		PerHit.Chance = 0.1f;
		const FSimResult Slow = Simulate(PerHit, 1.f, 20000.0, 1, 10.f, 12);
		const FSimResult Fast = Simulate(PerHit, 0.1f, 20000.0, 1, 10.f, 13);
		Report.Check(FMath::Abs(Slow.ProcsPerSecond() - 0.1f) < 0.01f && FMath::Abs(Fast.ProcsPerSecond() - 1.f) < 0.05f,
			FString::Printf(TEXT("10%% per hit: 1 hit/s -> %.3f procs/s, 10 hits/s -> %.3f procs/s (expect ~0.1 / ~1.0: scales with rate)"), Slow.ProcsPerSecond(), Fast.ProcsPerSecond()));

		// Shotgun: 1 attack = 8 pellets.
		FFPSRLBlessingTrigger PerAttack = PerHit;
		PerAttack.Scope = EFPSRLProcScope::OncePerAttack;
		const FSimResult ShotgunAttack = Simulate(PerAttack, 1.f, 20000.0, 8, 10.f, 14);
		const FSimResult ShotgunPellet = Simulate(PerHit, 1.f, 20000.0, 8, 10.f, 15);
		Report.Check(ShotgunAttack.Evaluations == 20000 && FMath::Abs(ShotgunAttack.ProcsPerSecond() - 0.1f) < 0.01f,
			FString::Printf(TEXT("shotgun, 10%% once per attack: %d rolls for 20000 blasts, %.3f procs/s (expect 20000 rolls, ~0.1)"), ShotgunAttack.Evaluations, ShotgunAttack.ProcsPerSecond()));
		Report.Check(ShotgunPellet.Evaluations == 160000 && FMath::Abs(ShotgunPellet.ProcsPerSecond() - 0.8f) < 0.03f,
			FString::Printf(TEXT("shotgun, 10%% per pellet: %d rolls, %.3f procs/s (expect 160000 rolls, ~0.8)"), ShotgunPellet.Evaluations, ShotgunPellet.ProcsPerSecond()));
		FFPSRLBlessingTrigger NormalizedPellet = MakeTrigger(EFPSRLProcModel::Normalized, Hit);
		NormalizedPellet.ProcsPerSecond = 1.f;
		const FSimResult NormalizedShotgun = Simulate(NormalizedPellet, 1.f, 20000.0, 8, 10.f, 16);
		Report.Check(FMath::Abs(NormalizedShotgun.ProcsPerSecond() - 1.f) < 0.05f,
			FString::Printf(TEXT("shotgun, normalized 1/s per pellet: %.3f procs/s (expect ~1.0: pellets don't multiply it)"), NormalizedShotgun.ProcsPerSecond()));

		// Melee: slow (1 swing/s) and fast (3 swings/s).
		FFPSRLBlessingTrigger MeleeNormalized = MakeTrigger(EFPSRLProcModel::Normalized, Attack);
		MeleeNormalized.ProcsPerSecond = 0.5f;
		const FSimResult SlowMelee = Simulate(MeleeNormalized, 1.f, 20000.0, 1, 75.f, 17);
		const FSimResult FastMelee = Simulate(MeleeNormalized, 1.f / 3.f, 20000.0, 1, 75.f, 18);
		Report.Check(FMath::Abs(SlowMelee.ProcsPerSecond() - 0.5f) < 0.03f && FMath::Abs(FastMelee.ProcsPerSecond() - 0.5f) < 0.03f,
			FString::Printf(TEXT("melee normalized 0.5/s: slow %.3f, fast %.3f procs/s (expect ~0.5 both)"), SlowMelee.ProcsPerSecond(), FastMelee.ProcsPerSecond()));
		FFPSRLBlessingTrigger MeleePerHit = PerHit;
		MeleePerHit.Chance = 0.3f;
		const FSimResult SlowMeleeHit = Simulate(MeleePerHit, 1.f, 20000.0, 1, 75.f, 19);
		const FSimResult FastMeleeHit = Simulate(MeleePerHit, 1.f / 3.f, 20000.0, 1, 75.f, 20);
		Report.Check(FMath::Abs(SlowMeleeHit.ProcsPerSecond() - 0.3f) < 0.02f && FMath::Abs(FastMeleeHit.ProcsPerSecond() - 0.9f) < 0.04f,
			FString::Printf(TEXT("melee 30%% per hit: slow %.3f, fast %.3f procs/s (expect ~0.3 / ~0.9)"), SlowMeleeHit.ProcsPerSecond(), FastMeleeHit.ProcsPerSecond()));

		// Internal cooldown: 100%, 3 s, events 10/s for 30 s -> t = 0, 3, ..., 27.
		FFPSRLBlessingTrigger Cooldown = MakeTrigger(EFPSRLProcModel::ChancePerEvent, Hit);
		Cooldown.InternalCooldown = 3.f;
		const FSimResult CooldownResult = Simulate(Cooldown, 0.1f, 30.0, 1, 10.f, 21);
		Report.Check(CooldownResult.Procs == 10, FString::Printf(TEXT("100%% with a 3 s cooldown, 10 hits/s for 30 s: %d procs (expect 10)"), CooldownResult.Procs));

		// Accumulation: every 10th hit; damage threshold 100 with 30-damage hits.
		FFPSRLBlessingTrigger Stacks = MakeTrigger(EFPSRLProcModel::Accumulate, Hit);
		Stacks.Threshold = 10.f;
		FPSRLProcs::FProcState StackState;
		int32 StackProcs = 0;
		for (int32 Index = 0; Index < 95; ++Index)
		{
			FPSRLProcs::FProcEvent Event;
			StackProcs += FPSRLProcs::Evaluate(Stacks, StackState, Event) ? 1 : 0;
		}
		Report.Check(StackProcs == 9 && FMath::IsNearlyEqual(StackState.Progress, 5.f), FString::Printf(TEXT("10 stacks: 95 hits -> %d procs, %.0f stacks left (expect 9, 5)"), StackProcs, StackState.Progress));
		FFPSRLBlessingTrigger DamageStacks = Stacks;
		DamageStacks.Threshold = 100.f;
		DamageStacks.AccumulateBy = EFPSRLProcAccumulation::Damage;
		const FSimResult DamageResult = Simulate(DamageStacks, 1.f, 12.0, 1, 30.f, 22);
		Report.Check(DamageResult.Procs == 3, FString::Printf(TEXT("every 100 damage, 12 hits of 30: %d procs (expect 3)"), DamageResult.Procs));

		// Conditions: critical only, minimum damage.
		FFPSRLBlessingTrigger CritOnly = MakeTrigger(EFPSRLProcModel::ChancePerEvent, Hit);
		CritOnly.bCriticalOnly = true;
		CritOnly.MinDamage = 20.f;
		FPSRLProcs::FProcState CritState;
		FPSRLProcs::FProcEvent Normal, Crit, WeakCrit;
		Normal.Damage = 50.f;
		Crit.Damage = 50.f;
		Crit.bCritical = true;
		WeakCrit.Damage = 10.f;
		WeakCrit.bCritical = true;
		const bool bNormal = FPSRLProcs::Evaluate(CritOnly, CritState, Normal);
		const bool bCrit = FPSRLProcs::Evaluate(CritOnly, CritState, Crit);
		const bool bWeak = FPSRLProcs::Evaluate(CritOnly, CritState, WeakCrit);
		Report.Check(!bNormal && bCrit && !bWeak, TEXT("critical-only, 20+ damage: normal hit no, critical 50 yes, critical 10 no"));

		// Normalized limits.
		FFPSRLBlessingTrigger Capped = MakeTrigger(EFPSRLProcModel::Normalized, Attack);
		Capped.ProcsPerSecond = 2.f;
		Capped.MaxChance = 0.75f;
		Capped.MinChance = 0.05f;
		FPSRLProcs::FProcEvent SlowEvent, FastEvent;
		SlowEvent.EventInterval = 1.f;
		FastEvent.EventInterval = 0.01f;
		Report.Check(FMath::IsNearlyEqual(FPSRLProcs::GetChance(Capped, SlowEvent), 0.75f) && FMath::IsNearlyEqual(FPSRLProcs::GetChance(Capped, FastEvent), 0.05f),
			TEXT("normalized limits: 2/s at 1 attack/s capped to 75%, at 100 attacks/s floored to 5%"));
	}

	UFPSRLBoonDefinition* MakeTestBoon(const TCHAR* Name, const TArray<EFPSRLItemSource>& Sources, const FFPSRLBlessingTrigger& Trigger, const TCHAR* Aspect = TEXT("DA_Aspect_Fire"))
	{
		UFPSRLBoonDefinition* Boon = NewObject<UFPSRLBoonDefinition>(GetTransientPackage(), Name);
		Boon->Aspect = LoadObject<UFPSRLAspectDefinition>(nullptr, *FString::Printf(TEXT("/Game/MainProject/Contents/Data/Aspects/%s.%s"), Aspect, Aspect));
		Boon->SupportedSources = Sources;
		Boon->BoonType = EFPSRLBoonType::Normal;
		Boon->Triggers.Add(Trigger);
		return Boon;
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

	AFPSRLWeapon* FindWeapon(UWorld* World, const APawn* Pawn)
	{
		for (TActorIterator<AFPSRLWeapon> It(World); It; ++It)
		{
			if (It->GetOwner() == Pawn && !It->IsHidden())
			{
				return *It;
			}
		}
		return nullptr;
	}

	float NearestEnemyHealth(UWorld* World, const APawn* Player)
	{
		float Best = MAX_flt;
		float Health = -1.f;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const UFPSRLHealthComponent* Component = It->IsPlayerControlled() ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
			const float Distance = Component ? FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) : MAX_flt;
			if (Distance < Best)
			{
				Best = Distance;
				Health = Component->GetCurrentHealth();
			}
		}
		return Health;
	}

	int32 Procs(const UFPSRLBoonComponent* Boons, const UFPSRLBoonDefinition* Boon, EFPSRLBoonChannel Channel)
	{
		const FPSRLProcs::FProcState* State = Boons->FindProcState(Boon, 0, Channel);
		return State ? State->ProcCount : 0;
	}

	/** The live part (host, solo): state kept between ticks. */
	struct FLive
	{
		FReport Report;
		int32 Step = 0;
		TStrongObjectPtr<UFPSRLBoonDefinition> Normalized, PerAttack, PerPellet, AttackEvent, MeleeAttack, MeleeHit, MeleeNormalized, Periodic;
		float HealthBefore = 0.f;
		float ChanceBefore = 0.f;
	};
}

static FAutoConsoleCommandWithWorld GFPSRLProcTestCommand(TEXT("FPSRL.ProcTest"),
	TEXT("Host test: Blessing proc models, simulated and live ([ProcTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLProcTest::FLive> Live = MakeShared<FPSRLProcTest::FLive>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Live](float)
		{
			using namespace FPSRLProcTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
			UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
			if (!Pawn || !Boons || !World->GetGameState())
			{
				return ++Live->Step < 400;
			}
			FReport& Report = Live->Report;
			UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
			const FGameplayTag Hit = FPSRLGameplayTags::Event_Hit;
			const FGameplayTag Attack = FPSRLGameplayTags::Event_Attack;
			const TCHAR* Shoot = TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot");
			const TCHAR* Melee = TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee");

			++Live->Step;
			if (Live->Step < 1000)
			{
				Live->Step = 1000;
				RunSimulations(Report);

				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				AFPSRLPlayerController::GiveWeaponToPawn(Pawn, LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C")));

				FFPSRLBlessingTrigger Trigger = MakeTrigger(EFPSRLProcModel::Normalized, Hit);
				Trigger.ProcsPerSecond = 1.f;
				Live->Normalized.Reset(MakeTestBoon(TEXT("TEST_Proc_Normalized"), { EFPSRLItemSource::Ranged }, Trigger));
				Trigger = MakeTrigger(EFPSRLProcModel::ChancePerEvent, Hit);
				Trigger.Scope = EFPSRLProcScope::OncePerAttack;
				Live->PerAttack.Reset(MakeTestBoon(TEXT("TEST_Proc_OncePerAttack"), { EFPSRLItemSource::Ranged }, Trigger));
				Trigger.Scope = EFPSRLProcScope::EachEvent;
				Live->PerPellet.Reset(MakeTestBoon(TEXT("TEST_Proc_PerPellet"), { EFPSRLItemSource::Ranged }, Trigger));
				Live->AttackEvent.Reset(MakeTestBoon(TEXT("TEST_Proc_OnAttack"), { EFPSRLItemSource::Ranged }, MakeTrigger(EFPSRLProcModel::ChancePerEvent, Attack)));
				Live->MeleeAttack.Reset(MakeTestBoon(TEXT("TEST_Proc_OnMeleeAttack"), { EFPSRLItemSource::Melee }, MakeTrigger(EFPSRLProcModel::ChancePerEvent, Attack), TEXT("DA_Aspect_Water")));
				Live->MeleeHit.Reset(MakeTestBoon(TEXT("TEST_Proc_OnMeleeHit"), { EFPSRLItemSource::Melee }, MakeTrigger(EFPSRLProcModel::ChancePerEvent, Hit), TEXT("DA_Aspect_Water")));
				Trigger = MakeTrigger(EFPSRLProcModel::Normalized, Attack);
				Trigger.ProcsPerSecond = 1.f;
				Live->MeleeNormalized.Reset(MakeTestBoon(TEXT("TEST_Proc_MeleeNormalized"), { EFPSRLItemSource::Melee }, Trigger, TEXT("DA_Aspect_Water")));

				const bool bGranted = Boons->GrantBoon(Live->Normalized.Get(), EFPSRLBoonChannel::Primary) && Boons->GrantBoon(Live->PerAttack.Get(), EFPSRLBoonChannel::Primary)
					&& Boons->GrantBoon(Live->PerPellet.Get(), EFPSRLBoonChannel::Primary) && Boons->GrantBoon(Live->AttackEvent.Get(), EFPSRLBoonChannel::Primary)
					&& Boons->GrantBoon(Live->MeleeAttack.Get(), EFPSRLBoonChannel::Secondary) && Boons->GrantBoon(Live->MeleeHit.Get(), EFPSRLBoonChannel::Secondary)
					&& Boons->GrantBoon(Live->MeleeNormalized.Get(), EFPSRLBoonChannel::Secondary);
				Report.Check(bGranted, TEXT("test Blessings granted (Primary: normalized, once per attack, per pellet, on attack; Secondary: on swing, on melee hit, normalized)"));
				return true;
			}
			switch (Live->Step)
			{
			case 1006:
			{
				// Fire-rate upgrade: the normalized chance follows the weapon's current rate.
				Live->ChanceBefore = Boons->GetCurrentProcChance(Live->Normalized.Get(), 0, EFPSRLBoonChannel::Primary);
				ASC->ApplyGameplayEffectToSelf(GetDefault<UFPSRLTestFireRateEffect>(), 1.f, ASC->MakeEffectContext());
				const float ChanceAfter = Boons->GetCurrentProcChance(Live->Normalized.Get(), 0, EFPSRLBoonChannel::Primary);
				Report.Check(Live->ChanceBefore > 0.f && FMath::IsNearlyEqual(ChanceAfter, Live->ChanceBefore * 0.5f, 0.001f),
					FString::Printf(TEXT("rifle normalized 1/s: chance per hit %.3f, after +100%% fire rate %.3f (expect half: the new rate applies at once)"), Live->ChanceBefore, ChanceAfter));

				// A shotgun: this rifle fires 8 pellets as one attack.
				if (AFPSRLWeapon* Weapon = FindWeapon(World, Pawn))
				{
					Weapon->ProjectilesPerShot = 8;
					Weapon->AimVariance = 0.f;
				}
				const float PelletChance = Boons->GetCurrentProcChance(Live->Normalized.Get(), 0, EFPSRLBoonChannel::Primary);
				Report.Check(FMath::IsNearlyEqual(PelletChance, ChanceAfter / 8.f, 0.001f), FString::Printf(TEXT("8 pellets per shot: normalized per-hit chance %.4f (expect %.4f, per pellet)"), PelletChance, ChanceAfter / 8.f));
				Press(PC, Melee);	// no enemy yet: a swing that misses
				break;
			}
			case 1008:
				Report.Check(Procs(Boons, Live->MeleeAttack.Get(), EFPSRLBoonChannel::Secondary) == 1 && Procs(Boons, Live->MeleeHit.Get(), EFPSRLBoonChannel::Secondary) == 0
					&& Procs(Boons, Live->AttackEvent.Get(), EFPSRLBoonChannel::Primary) == 0,
					FString::Printf(TEXT("a melee swing that missed: on-swing %d, on-melee-hit %d, the gun's on-attack %d (expect 1, 0, 0)"),
						Procs(Boons, Live->MeleeAttack.Get(), EFPSRLBoonChannel::Secondary), Procs(Boons, Live->MeleeHit.Get(), EFPSRLBoonChannel::Secondary),
						Procs(Boons, Live->AttackEvent.Get(), EFPSRLBoonChannel::Primary)));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());
				break;
			case 1012:
				Live->HealthBefore = NearestEnemyHealth(World, Pawn);
				Press(PC, Shoot);
				break;
			case 1016:
			{
				const float Taken = Live->HealthBefore - NearestEnemyHealth(World, Pawn);
				const int32 PelletsHit = FMath::RoundToInt(Taken / 12.f);
				const int32 Attacks = Procs(Boons, Live->AttackEvent.Get(), EFPSRLBoonChannel::Primary);
				const int32 OncePer = Procs(Boons, Live->PerAttack.Get(), EFPSRLBoonChannel::Primary);
				const int32 PerPellet = Procs(Boons, Live->PerPellet.Get(), EFPSRLBoonChannel::Primary);
				Report.Check(PelletsHit >= 2 && Attacks == 1 && OncePer == 1 && PerPellet == PelletsHit,
					FString::Printf(TEXT("one shotgun blast, %d pellets hit (%.0f damage): on-attack procs %d, once-per-attack %d, per-pellet %d (expect 1, 1, %d)"),
						PelletsHit, Taken, Attacks, OncePer, PerPellet, PelletsHit));
				break;
			}
			case 1018:
				Press(PC, Melee);	// 3 s after the first swing: hits the enemy 1.5 m ahead
				break;
			case 1020:
			{
				const int32 Swings = Procs(Boons, Live->MeleeAttack.Get(), EFPSRLBoonChannel::Secondary);
				const int32 Hits = Procs(Boons, Live->MeleeHit.Get(), EFPSRLBoonChannel::Secondary);
				Report.Check(Swings == 2 && Hits == 1, FString::Printf(TEXT("a melee swing that hit: on-swing %d, on-melee-hit %d (expect 2, 1)"), Swings, Hits));

				// Melee speed: slow (1 swing/s) vs three times faster.
				const float SlowChance = Boons->GetCurrentProcChance(Live->MeleeNormalized.Get(), 0, EFPSRLBoonChannel::Secondary);
				ASC->ApplyGameplayEffectToSelf(GetDefault<UFPSRLTestMeleeSpeedEffect>(), 1.f, ASC->MakeEffectContext());
				const float FastChance = Boons->GetCurrentProcChance(Live->MeleeNormalized.Get(), 0, EFPSRLBoonChannel::Secondary);
				Report.Check(FMath::IsNearlyEqual(SlowChance, 1.f, 0.001f) && FMath::IsNearlyEqual(FastChance, 1.f / 3.f, 0.01f),
					FString::Printf(TEXT("melee normalized 1/s: chance per swing %.3f at 1 swing/s, %.3f at 3 swings/s (expect 1.000 / 0.333)"), SlowChance, FastChance));

				// Periodic: every 0.5 s, 5 damage to enemies within 5 m, from a timer (no events, no Tick).
				FFPSRLBlessingTrigger Trigger = MakeTrigger(EFPSRLProcModel::Periodic, FGameplayTag());
				Trigger.Interval = 0.5f;
				FFPSRLBlessingAction& Pulse = Trigger.Actions.AddDefaulted_GetRef();
				Pulse.Target = EFPSRLBlessingActionTarget::AreaAroundSelf;
				Pulse.Radius = 500.f;
				Pulse.Effect = UFPSRLDamageEffect::StaticClass();
				Pulse.MagnitudeTag = FPSRLGameplayTags::SetByCaller_Damage;
				Pulse.Magnitude = 5.f;
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("130"), FString());	// a fresh target
				Live->Periodic.Reset(MakeTestBoon(TEXT("TEST_Proc_Periodic"), {}, Trigger));
				Report.Check(Boons->GrantBoon(Live->Periodic.Get(), EFPSRLBoonChannel::Primary), TEXT("periodic test Blessing granted"));
				Live->HealthBefore = NearestEnemyHealth(World, Pawn);
				break;
			}
			case 1029:
			{
				// 2.25 s later: pulses at 0.5, 1.0, 1.5, 2.0 s.
				const int32 Pulses = Procs(Boons, Live->Periodic.Get(), EFPSRLBoonChannel::Primary);
				const float Taken = Live->HealthBefore - NearestEnemyHealth(World, Pawn);
				Report.Check(Pulses == 4 && FMath::IsNearlyEqual(Taken, 20.f, 0.5f),
					FString::Printf(TEXT("periodic 0.5 s for 2.25 s: %d pulses, near enemy took %.0f (expect 4, 20)"), Pulses, Taken));
				UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] done: %d problem(s)"), Report.Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			default:
				break;
			}
			return true;
		}), 0.25f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRLProcMultiTestCommand(TEXT("FPSRL.ProcMultiTest"),
	TEXT("Host test: with N players, every player owns the same Blessing; each one's hits only move their own proc state ([ProcTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		const int32 Wanted = Args.IsEmpty() ? 1 : FMath::Max(1, FCString::Atoi(*Args[0]));
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		TSharedRef<TStrongObjectPtr<UFPSRLBoonDefinition>> Shared = MakeShared<TStrongObjectPtr<UFPSRLBoonDefinition>>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step, Shared, Wanted](float)
		{
			using namespace FPSRLProcTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* HostPC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			TArray<AFPSRLPlayerState*> Players;
			for (APlayerState* Player : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
			{
				AFPSRLPlayerState* State = Cast<AFPSRLPlayerState>(Player);
				if (State && State->GetPawn() && State->GetBoonComponent() && Cast<AFPSRLPlayerController>(State->GetPawn()->GetController()))
				{
					Players.Add(State);
				}
			}
			++*Step;
			if (*Step < 1000)
			{
				if (!HostPC || Players.Num() < Wanted)
				{
					if (*Step > 900)
					{
						UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] PROBLEM only %d of %d players joined"), Players.Num(), Wanted);
						if (HostPC)
						{
							HostPC->ConsoleCommand(TEXT("quit"));
						}
						return false;
					}
					return true;
				}
				*Step = 1000;
				FFPSRLBlessingTrigger Trigger = MakeTrigger(EFPSRLProcModel::Accumulate, FPSRLGameplayTags::Event_Hit);
				Trigger.Threshold = 1000.f;	// never procs: only the counters are compared
				Shared->Reset(MakeTestBoon(TEXT("TEST_Proc_SharedStacks"), { EFPSRLItemSource::Melee }, Trigger));
				for (AFPSRLPlayerState* Player : Players)
				{
					const bool bGranted = Player->GetBoonComponent()->GrantBoon(Shared->Get(), EFPSRLBoonChannel::Secondary);
					UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] %s owns the shared test Blessing: %s"), *Player->GetPlayerName(), bGranted ? TEXT("yes") : TEXT("NO"));
				}
				HostPC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("400"), FString());
				return true;
			}
			if (*Step == 1004)
			{
				APawn* Enemy = nullptr;
				for (TActorIterator<APawn> It(World); It && !Enemy; ++It)
				{
					Enemy = It->IsPlayerControlled() ? nullptr : *It;
				}
				// Player i lands i+1 melee hits (server-side damage from that player's pawn and controller).
				for (int32 Index = 0; Index < Players.Num(); ++Index)
				{
					APawn* PlayerPawn = Players[Index]->GetPawn();
					for (int32 HitCount = 0; Enemy && HitCount <= Index; ++HitCount)
					{
						UGameplayStatics::ApplyDamage(Enemy, 1.f, PlayerPawn->GetController(), PlayerPawn, nullptr);
					}
				}
				int32 Problems = 0;
				for (int32 Index = 0; Index < Players.Num(); ++Index)
				{
					const FPSRLProcs::FProcState* State = Players[Index]->GetBoonComponent()->FindProcState(Shared->Get(), 0, EFPSRLBoonChannel::Secondary);
					const float Progress = State ? State->Progress : 0.f;
					const bool bOk = FMath::IsNearlyEqual(Progress, static_cast<float>(Index + 1));
					Problems += bOk ? 0 : 1;
					UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] %s player %d of %d (%s): %.0f stacks from own hits (expect %d)"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"),
						Index + 1, Players.Num(), *Players[Index]->GetPlayerName(), Progress, Index + 1);
				}
				UE_LOG(LogFPSRL, Log, TEXT("[ProcTest] done: %d problem(s) with %d player(s)"), Problems, Players.Num());
				HostPC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.25f);
	}));
#endif
