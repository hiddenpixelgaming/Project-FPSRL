// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.TeamHUDTest <players> (the host of a listen game, any level with pawns, e.g. the Lobby). Waits for
// <players> players, then checks the host's teammate HUD: one entry per OTHER player, never the host; damage, healing,
// max health up / down, the downed state and the speaking state reach the right entry. Clients started with
// "fpsrl.TeamHUD.LogHealth 1" log what their own teammate HUD shows at every change ([TeamHUD] lines), so their logs
// can be checked against the host's values. Ends with "[TeamHUDTest] done: N problem(s)".

#include "AbilitySystemComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "UI/FPSRLTeamHUDWidget.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLTeamHUDTest
{
	struct FState
	{
		int32 Players = 2;
		int32 Step = 0;
		int32 Problems = 0;
		double StepTime = 0.0;
		double StartTime = 0.0;
	};

	static UFPSRLHealthComponent* HealthOf(const APlayerState* PlayerState)
	{
		const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLTeamHUDTestCommand(TEXT("FPSRL.TeamHUDTest"),
	TEXT("Test (host): FPSRL.TeamHUDTest <players>. Teammate HUD entries, health, downed and speaking states ([TeamHUDTest])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLTeamHUDTest::FState> S = MakeShared<FPSRLTeamHUDTest::FState>();
		S->Players = Args.IsEmpty() ? 2 : FMath::Clamp(FCString::Atoi(*Args[0]), 1, 4);
		S->StartTime = S->StepTime = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			using namespace FPSRLTeamHUDTest;
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* Host = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			UFPSRLTeamHUDWidget* HUD = Host ? Host->GetTeamHUD() : nullptr;
			const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			const double Now = FPlatformTime::Seconds();
			auto Check = [S](bool bOk, const FString& What)
			{
				S->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[TeamHUDTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto Finish = [S]()
			{
				UE_LOG(LogFPSRL, Log, TEXT("[TeamHUDTest] done: %d problem(s)"), S->Problems);
				return false;
			};
			if (!HUD || !GameState)
			{
				if (Now - S->StartTime > 120.0)
				{
					Check(false, TEXT("no host controller / teammate HUD"));
					return Finish();
				}
				return true;
			}
			// Remote players (with a pawn), in PlayerId order: what the HUD should list.
			TArray<APlayerState*> Others;
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				if (PlayerState && PlayerState != Host->PlayerState && PlayerState->GetPawn())
				{
					Others.Add(PlayerState);
				}
			}
			Others.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });
			auto Expected = [&Others](const TCHAR* Suffix0 = TEXT(""))
			{
				TArray<FString> Parts;
				for (int32 Index = 0; Index < Others.Num(); ++Index)
				{
					const UFPSRLHealthComponent* Health = HealthOf(Others[Index]);
					Parts.Add(FString::Printf(TEXT("%s %d/%d%s"), *Others[Index]->GetPlayerName(), Health ? FMath::CeilToInt(Health->GetCurrentHealth()) : -1,
						Health ? FMath::CeilToInt(Health->GetMaxHealth()) : -1, Index == 0 ? Suffix0 : TEXT("")));
				}
				return FString::Printf(TEXT("%d teammate(s)%s%s"), Others.Num(), Others.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Parts, TEXT(" | ")));
			};
			const double Elapsed = Now - S->StepTime;
			auto Next = [S, Now]() { ++S->Step; S->StepTime = Now; };

			switch (S->Step)
			{
			case 0:	// wait for everyone (and their pawns), then let replication settle
				if (Others.Num() + 1 < S->Players)
				{
					if (Now - S->StartTime > 180.0)
					{
						Check(false, FString::Printf(TEXT("only %d of %d players arrived"), Others.Num() + 1, S->Players));
						return Finish();
					}
					S->StepTime = Now;
					return true;
				}
				if (Elapsed > 4.0)
				{
					Next();
				}
				return true;
			case 1:
			{
				const FString Shown = HUD->DescribeForTest();
				Check(Shown == Expected(), FString::Printf(TEXT("%d players: host's teammate HUD '%s' (expect '%s')"), S->Players, *Shown, *Expected()));
				Check(!Shown.Contains(Host->PlayerState->GetPlayerName() + TEXT(" ")), FString::Printf(TEXT("the host (%s) is not listed"), *Host->PlayerState->GetPlayerName()));
				// Damage: each teammate a different amount.
				for (int32 Index = 0; Index < Others.Num(); ++Index)
				{
					if (UFPSRLHealthComponent* Health = HealthOf(Others[Index]))
					{
						Health->ApplyEnvironmentDamage(15.f * (Index + 1));
					}
				}
				Next();
				return true;
			}
			case 2:
				if (Elapsed > 1.5)
				{
					Check(HUD->DescribeForTest() == Expected(), FString::Printf(TEXT("after damage: '%s' (expect '%s')"), *HUD->DescribeForTest(), *Expected()));
					for (APlayerState* Other : Others)
					{
						if (UFPSRLHealthComponent* Health = HealthOf(Other))
						{
							Health->Heal(10.f);
						}
					}
					Next();
				}
				return true;
			case 3:
				if (Elapsed > 1.5)
				{
					Check(HUD->DescribeForTest() == Expected(), FString::Printf(TEXT("after healing 10: '%s' (expect '%s')"), *HUD->DescribeForTest(), *Expected()));
					for (APlayerState* Other : Others)
					{
						if (UFPSRLHealthComponent* Health = HealthOf(Other))
						{
							Health->SetMaxHealthServer(150.f);
						}
					}
					Next();
				}
				return true;
			case 4:
				if (Elapsed > 1.5)
				{
					Check(HUD->DescribeForTest() == Expected(), FString::Printf(TEXT("max health raised to 150: '%s' (expect '%s')"), *HUD->DescribeForTest(), *Expected()));
					for (APlayerState* Other : Others)
					{
						if (UFPSRLHealthComponent* Health = HealthOf(Other))
						{
							Health->SetMaxHealthServer(60.f);
						}
					}
					Next();
				}
				return true;
			case 5:
				if (Elapsed > 1.5)
				{
					Check(HUD->DescribeForTest() == Expected(), FString::Printf(TEXT("max health lowered to 60: '%s' (expect '%s')"), *HUD->DescribeForTest(), *Expected()));
					// Speaking: every teammate, plus the host's own id (no entry: ignored).
					for (APlayerState* Other : Others)
					{
						HUD->SetSpeakingState(Other->GetPlayerId(), true);
					}
					HUD->SetSpeakingState(Host->PlayerState->GetPlayerId(), true);
					if (!IsRunningCommandlet() && FApp::CanEverRender())
					{
						Host->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: the teammate HUD with speakers
					}
					Next();
				}
				return true;
			case 6:
			{
				if (Elapsed < 0.5)
				{
					return true;	// the screenshot is taken on a later frame
				}
				const FString Shown = HUD->DescribeForTest();
				int32 Speaking = 0;
				for (int32 At = Shown.Find(TEXT("[speaking]")); At != INDEX_NONE; At = Shown.Find(TEXT("[speaking]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
				{
					++Speaking;
				}
				Check(Speaking == Others.Num() && HUD->GetTeammateCount() == Others.Num(), FString::Printf(TEXT("all %d teammates speaking at once, the host's own id ignored: '%s'"), Others.Num(), *Shown));
				if (!Others.IsEmpty())
				{
					HUD->SetSpeakingState(Others[0]->GetPlayerId(), false);
					Check(!HUD->DescribeForTest().StartsWith(Expected(TEXT(" [speaking]"))) && Speaking > 0, FString::Printf(TEXT("the first stops speaking, the others still are: '%s'"), *HUD->DescribeForTest()));
				}
				for (APlayerState* Other : Others)
				{
					HUD->SetSpeakingState(Other->GetPlayerId(), false);
				}
				// Downed: the first teammate (stays listed).
				if (UFPSRLHealthComponent* Health = Others.IsEmpty() ? nullptr : HealthOf(Others[0]))
				{
					if (UAbilitySystemComponent* ASC = Cast<AFPSRLPlayerState>(Others[0]) ? Cast<AFPSRLPlayerState>(Others[0])->GetAbilitySystemComponent() : nullptr; ASC && Others.Num() > 1)
					{
						ASC->AddLooseGameplayTag(FPSRLGameplayTags::Status_Downable, 1, EGameplayTagReplicationState::TagOnly);	// as in a run (needs someone up to revive)
					}
					Health->ApplyEnvironmentDamage(Health->GetCurrentHealth() + 10.f);
				}
				Next();
				return true;
			}
			case 7:
				if (Elapsed > 1.5)
				{
					if (!Others.IsEmpty())
					{
						const UFPSRLHealthComponent* Health = HealthOf(Others[0]);
						Check(HUD->GetTeammateCount() == Others.Num() && HUD->DescribeForTest().Contains(Health && Health->IsDowned() ? TEXT("[DOWN]") : TEXT("[DEAD]")),
							FString::Printf(TEXT("first teammate down (%s): still listed, '%s'"), Health && Health->IsDowned() ? TEXT("downed") : TEXT("dead"), *HUD->DescribeForTest()));
						if (UFPSRLHealthComponent* Downed = HealthOf(Others[0]); Downed && Downed->IsDowned())
						{
							Downed->Revive(1.f);
						}
					}
					for (APlayerState* Other : Others)
					{
						if (UFPSRLHealthComponent* Health = HealthOf(Other))
						{
							Health->SetMaxHealthServer(100.f);
							Health->Heal(100.f);
						}
					}
					Next();
				}
				return true;
			case 8:
				if (Elapsed > 1.5)
				{
					Check(HUD->DescribeForTest() == Expected(), FString::Printf(TEXT("back to full: '%s' (expect '%s')"), *HUD->DescribeForTest(), *Expected()));
					return Finish();
				}
				return true;
			default:
				return Finish();
			}
		}));
	}));
#endif
