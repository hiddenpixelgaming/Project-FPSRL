// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.BlessingTest checks the Blessing behaviour framework headless with in-memory TEST Blessings
// (never assets, never in the pool): triggers, conditional damage, a kill area effect, a stat grant and source filtering.

#include "Abilities/Effects/FPSRLHealthEffects.h"
#include "Abilities/Effects/FPSRLStatusEffects.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Combat/FPSRLWeapon.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "Types/FPSRLGameplayTags.h"
#include "UObject/Package.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLBlessingTest
{
	FString DescribeEnemies(UWorld* World, const APawn* Player)
	{
		TArray<APawn*> Found;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (!It->IsPlayerControlled())
			{
				Found.Add(*It);
			}
		}
		Found.Sort([Player](const APawn& A, const APawn& B) { return FVector::Dist(A.GetActorLocation(), Player->GetActorLocation()) < FVector::Dist(B.GetActorLocation(), Player->GetActorLocation()); });
		FString Out;
		for (const APawn* Enemy : Found)
		{
			const UFPSRLHealthComponent* Health = Enemy->FindComponentByClass<UFPSRLHealthComponent>();
			Out += FString::Printf(TEXT("[%.0f cm: %.1f hp] "), FVector::Dist(Enemy->GetActorLocation(), Player->GetActorLocation()), Health ? Health->GetCurrentHealth() : -1.0);
		}
		return Out;
	}

	void Press(AFPSRLPlayerController* PC, const TCHAR* ActionPath)
	{
		const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		if (UInputAction* Action = LoadObject<UInputAction>(nullptr, ActionPath); Action && Input)
		{
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});	// a real key press for every binding
		}
	}

	UFPSRLBoonDefinition* MakeTestBoon(const TCHAR* Name, const TCHAR* Aspect, const TArray<EFPSRLItemSource>& Sources)
	{
		UFPSRLBoonDefinition* Boon = NewObject<UFPSRLBoonDefinition>(GetTransientPackage(), Name);
		Boon->Aspect = LoadObject<UFPSRLAspectDefinition>(nullptr, *FString::Printf(TEXT("/Game/MainProject/Contents/Data/Aspects/%s.%s"), Aspect, Aspect));
		Boon->SupportedSources = Sources;
		Boon->BoonType = EFPSRLBoonType::Normal;
		return Boon;
	}

	FFPSRLBlessingAction& AddAction(FFPSRLBlessingTrigger& Trigger, TSubclassOf<UGameplayEffect> Effect, float Magnitude)
	{
		FFPSRLBlessingAction& Action = Trigger.Actions.AddDefaulted_GetRef();
		Action.Effect = Effect;
		Action.MagnitudeTag = FPSRLGameplayTags::SetByCaller_Damage;
		Action.Magnitude = Magnitude;
		return Action;
	}

	void GrantTestBlessings(AFPSRLPlayerState* PS)
	{
		UFPSRLBoonComponent* Boons = PS->GetBoonComponent();

		UFPSRLBoonDefinition* MarkOnHit = MakeTestBoon(TEXT("TEST_Ranged_MarkOnHit"), TEXT("DA_Aspect_Fire"), { EFPSRLItemSource::Ranged });
		FFPSRLBlessingTrigger& Mark = MarkOnHit->Triggers.AddDefaulted_GetRef();
		Mark.Event = FPSRLGameplayTags::Event_Hit;
		AddAction(Mark, UFPSRLTestMarkEffect::StaticClass(), 10.f);

		UFPSRLBoonDefinition* BonusVsMarked = MakeTestBoon(TEXT("TEST_Universal_BonusVsMarked"), TEXT("DA_Aspect_Fire"), {});
		FFPSRLBlessingDamageBonus& Bonus = BonusVsMarked->DamageBonuses.AddDefaulted_GetRef();
		Bonus.TargetRequirements.RequireTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Test.Marked")));
		Bonus.Bonus = 0.5f;

		UFPSRLBoonDefinition* KillNova = MakeTestBoon(TEXT("TEST_Universal_KillNova"), TEXT("DA_Aspect_Fire"), {});
		FFPSRLBlessingTrigger& Nova = KillNova->Triggers.AddDefaulted_GetRef();
		Nova.Event = FPSRLGameplayTags::Event_Kill;
		FFPSRLBlessingAction& NovaAction = AddAction(Nova, UFPSRLDamageEffect::StaticClass(), 30.f);
		NovaAction.Target = EFPSRLBlessingActionTarget::AreaAroundTarget;
		NovaAction.Radius = 400.f;

		UFPSRLBoonDefinition* RangedStat = MakeTestBoon(TEXT("TEST_Universal_RangedDamageStat"), TEXT("DA_Aspect_Fire"), {});
		RangedStat->Grants.Effects.Add(UFPSRLTestRangedDamageEffect::StaticClass());

		UFPSRLBoonDefinition* MeleeMark = MakeTestBoon(TEXT("TEST_Melee_MarkOnHit"), TEXT("DA_Aspect_Water"), { EFPSRLItemSource::Melee });
		FFPSRLBlessingTrigger& Melee = MeleeMark->Triggers.AddDefaulted_GetRef();
		Melee.Event = FPSRLGameplayTags::Event_Hit;
		AddAction(Melee, UFPSRLTestMarkEffect::StaticClass(), 5.f);

		// Source rules: the melee-only Blessing must be refused on the gun slot.
		UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] melee-only Blessing refused on the Primary gun: %s"),
			Boons->GrantBoon(MeleeMark, EFPSRLBoonChannel::Primary) ? TEXT("NO (wrong)") : TEXT("yes"));
		const bool bGranted = Boons->GrantBoon(MarkOnHit, EFPSRLBoonChannel::Primary) && Boons->GrantBoon(BonusVsMarked, EFPSRLBoonChannel::Primary)
			&& Boons->GrantBoon(KillNova, EFPSRLBoonChannel::Primary) && Boons->GrantBoon(RangedStat, EFPSRLBoonChannel::Primary)
			&& Boons->GrantBoon(MeleeMark, EFPSRLBoonChannel::Secondary);
		UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] test Blessings granted: %s"), bGranted ? TEXT("yes") : TEXT("NO"));
	}
}

static FAutoConsoleCommandWithWorld GFPSRLBlessingTestCommand(TEXT("FPSRL.BlessingTest"),
	TEXT("Host test: Blessing triggers, conditional damage, kill area effect, stat grant, source filtering ([BlessingTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step](float)
		{
			using namespace FPSRLBlessingTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
			if (!Pawn || !PS || !World->GetGameState())
			{
				return ++*Step < 400;
			}
			++*Step;
			if (*Step < 1000)
			{
				*Step = 1000;
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				AFPSRLPlayerController::GiveWeaponToPawn(Pawn, LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C")));
				GrantTestBlessings(PS);
				// Fixed reference numbers whatever the current tuning: pistol 50 per shot, melee 75.
				PC->MeleeDamage = 75.f;
				for (TActorIterator<AFPSRLWeapon> It(World); It; ++It)
				{
					if (It->GetOwner() == Pawn)
					{
						It->ShotDamage = 50.f;
					}
				}
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("300"), TEXT("120"));
				return true;
			}
			switch (*Step)
			{
			case 1008:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] before: %s"), *DescribeEnemies(World, Pawn));
				Press(PC, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot"));	// Pistol 50 x 1.5 (stat) = 75, then marked
				break;
			case 1010:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] 0.5 s after shot 1 (expect near 25): %s"), *DescribeEnemies(World, Pawn));
				break;
			case 1015:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] 1.75 s after shot 1 (one 10 burn tick: expect near 15): %s"), *DescribeEnemies(World, Pawn));
				Press(PC, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot"));	// vs marked: 75 x 1.5 = 112.5 -> kill -> nova 30
				break;
			case 1018:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] after shot 2 (near dead; far one hit by the 30 nova: expect 70): %s"), *DescribeEnemies(World, Pawn));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("150"), FString());
				break;
			case 1022:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] melee target spawned: %s"), *DescribeEnemies(World, Pawn));
				Press(PC, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee"));	// 75 (no ranged bonus), melee Blessing marks at 5/s
				break;
			case 1024:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] after melee (expect the near one at 25): %s"), *DescribeEnemies(World, Pawn));
				break;
			case 1031:
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] 2.25 s after melee (5/s ticks: expect 15): %s"), *DescribeEnemies(World, Pawn));
				UE_LOG(LogFPSRL, Log, TEXT("[BlessingTest] done"));
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			default:
				break;
			}
			return true;
		}), 0.25f);
	}));
#endif
