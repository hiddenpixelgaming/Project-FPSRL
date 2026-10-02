// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.FeedbackTest checks combat feedback on this machine (host or client): a pistol hit and a kill show
// the hit / kill marker; the melee reach brackets appear only with an enemy in reach; a swing lands after its windup,
// shows the melee marker and knocks the enemy back; a swing at nothing reports a miss.

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
#include "UI/FPSRLCombatHUDWidget.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLFeedbackTest
{
	APawn* NearestEnemy(UWorld* World, const APawn* Player)
	{
		APawn* Best = nullptr;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const UFPSRLHealthComponent* Health = It->IsPlayerControlled() ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
			if (Health && Health->GetCurrentHealth() > 0.f && (!Best || FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < FVector::Dist(Best->GetActorLocation(), Player->GetActorLocation())))
			{
				Best = *It;
			}
		}
		return Best;
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

	struct FState
	{
		int32 Step = 0;
		int32 Problems = 0;
		float HealthMark = 0.f;
		FVector PositionMark = FVector::ZeroVector;
		TWeakObjectPtr<APawn> Enemy;
	};
}

static FAutoConsoleCommandWithWorld GFPSRLFeedbackTestCommand(TEXT("FPSRL.FeedbackTest"),
	TEXT("Test: hit / kill markers, melee windup, hit, knockback, miss and reach brackets ([FeedbackTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLFeedbackTest::FState> S = MakeShared<FPSRLFeedbackTest::FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			using namespace FPSRLFeedbackTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			const bool bInGame = World && (World->GetNetMode() == NM_Client ? PC && PC->GetNetConnection() != nullptr : World->GetGameState() != nullptr);
			if (!Pawn || !bInGame || !PC->GetCombatHUD())
			{
				return ++S->Step < 400;
			}
			auto Check = [S](bool bOk, const FString& What)
			{
				S->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[FeedbackTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			const FString Hud = PC->GetCombatHUD()->DescribeForTest();
			const FString Marker = Hud.Mid(Hud.Find(TEXT("hit marker")));
			const TCHAR* Shoot = TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot");
			const TCHAR* MeleeAction = TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee");
			APawn* Enemy = S->Enemy.Get();
			auto EnemyHealth = [Enemy]() { const UFPSRLHealthComponent* H = Enemy ? Enemy->FindComponentByClass<UFPSRLHealthComponent>() : nullptr; return H ? H->GetCurrentHealth() : -1.f; };

			++S->Step;
			if (S->Step < 1000)
			{
				S->Step = 1000;
				PC->ServerTestCommand(TEXT("God"), FString(), FString());
				AFPSRLPlayerController::GiveWeaponToPawn(Pawn, LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C")));
				PC->ServerTestCommand(TEXT("SpawnTestEnemy"), TEXT("400"), FString());
				PC->MeleeDamage = 75.f;	// the melee target (100 health) must survive the swing (on a client this is only the local copy)
				return true;
			}
			switch (S->Step)
			{
			case 1008:
				S->Enemy = NearestEnemy(World, Pawn);
				Check(Hud.Contains(TEXT("melee reach hidden")), FString::Printf(TEXT("enemy 4 m away: %s (expect no reach brackets)"), *Marker));
				Press(PC, Shoot);
				break;
			case 1011:
				Check(Marker.StartsWith(TEXT("hit marker Hit")) && Hud.Contains(TEXT("(1 shown)")), FString::Printf(TEXT("pistol hit: %s; enemy at %.0f (expect a Hit marker)"), *Marker, EnemyHealth()));
				{
					// The damage number shows what the enemy actually lost, in white (no critical).
					const FString Numbers = Hud.Mid(Hud.Find(TEXT("damage numbers")));
					const UFPSRLHealthComponent* H = Enemy ? Enemy->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
					const FString Expected = FString::Printf(TEXT("last '%d' white"), H ? FMath::RoundToInt(H->GetMaxHealth() - H->GetCurrentHealth()) : -1);
					Check(Numbers.StartsWith(TEXT("damage numbers 1 on screen")) && Numbers.Contains(Expected), FString::Printf(TEXT("pistol hit: %s (expect 1 number, %s)"), *Numbers, *Expected));
					if (FApp::CanEverRender())
					{
						PC->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: a screenshot with the number on screen
					}
				}
				Press(PC, Shoot);	// 50 + 50 = the 100-health test enemy
				break;
			case 1014:
				Check(Marker.StartsWith(TEXT("hit marker Kill")), FString::Printf(TEXT("second shot kills: %s; enemy at %.0f (expect a Kill marker)"), *Marker, EnemyHealth()));
				Press(PC, MeleeAction);	// nothing in reach: a miss
				break;
			case 1018:
				Check(Marker.StartsWith(TEXT("hit marker MeleeMiss")), FString::Printf(TEXT("swing at nothing: %s (expect MeleeMiss)"), *Marker));
				PC->ServerTestCommand(TEXT("SpawnTestAI"), TEXT("150"), TEXT("0"));	// with its (inactive) AI: an uncontrolled character can't be pushed
				break;
			case 1022:
				S->Enemy = NearestEnemy(World, Pawn);
				Check(Hud.Contains(TEXT("melee reach shown")), FString::Printf(TEXT("enemy 1.5 m ahead: %s (expect the reach brackets)"), *Marker));
				break;
			case 1027:
				S->HealthMark = EnemyHealth();
				S->PositionMark = Enemy ? Enemy->GetActorLocation() : FVector::ZeroVector;
				Press(PC, MeleeAction);	// 1.3 s after the miss: off cooldown
				break;
			case 1028:
				Check(FMath::IsNearlyEqual(EnemyHealth(), S->HealthMark), FString::Printf(TEXT("0.1 s after the press: enemy at %.0f (expect untouched: the swing is still winding up)"), EnemyHealth()));
				break;
			case 1032:
			{
				const float Pushed = Enemy ? FVector::Dist2D(Enemy->GetActorLocation(), S->PositionMark) : -1.f;
				Check(EnemyHealth() < S->HealthMark && ((Marker.StartsWith(TEXT("hit marker MeleeHit")) && Pushed > 40.f) || Marker.StartsWith(TEXT("hit marker MeleeKill"))),
					FString::Printf(TEXT("swing landed: enemy %.0f -> %.0f, pushed %.0f cm; %s (expect damage and a MeleeHit marker with knockback, or MeleeKill)"), S->HealthMark, EnemyHealth(), Pushed, *Marker));
				UE_LOG(LogFPSRL, Log, TEXT("[FeedbackTest] done: %d problem(s)"), S->Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			default:
				break;
			}
			return true;
		}), 0.1f);
	}));
#endif
