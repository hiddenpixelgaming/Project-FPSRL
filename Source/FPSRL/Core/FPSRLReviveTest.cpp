// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.ReviveTest (host of a two-player game, any level with pawns): the client is downed, the host starts
// reviving; damage to the reviver below ReviveInterruptDamage keeps the revive going, reaching it interrupts; a new
// revive starts its count from zero and completes after ReviveSeconds.

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Containers/Ticker.h"
#include "Components/PrimitiveComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Rooms/FPSRLReviveMarker.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLReviveTest
{
	struct FState
	{
		int32 Step = 0;
		int32 Problems = 0;
		TWeakObjectPtr<APawn> Downed;
	};
}

static FAutoConsoleCommandWithWorld GFPSRLReviveTestCommand(TEXT("FPSRL.ReviveTest"),
	TEXT("Test (host, 2 players): damage to a reviver interrupts the revive at ReviveInterruptDamage ([ReviveTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLReviveTest::FState> S = MakeShared<FPSRLReviveTest::FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* Host = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			APawn* HostPawn = Host ? Host->GetPawn() : nullptr;
			APawn* Other = nullptr;
			if (World)
			{
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (It->Get() && It->Get() != Host && It->Get()->GetPawn())
					{
						Other = It->Get()->GetPawn();
					}
				}
			}
			if (!HostPawn || !Other)
			{
				return ++S->Step < 1200;	// waiting for the second player
			}
			auto Check = [S](bool bOk, const FString& What)
			{
				S->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[ReviveTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			UFPSRLHealthComponent* HostHealth = HostPawn->FindComponentByClass<UFPSRLHealthComponent>();
			UFPSRLHealthComponent* OtherHealth = Other->FindComponentByClass<UFPSRLHealthComponent>();
			AFPSRLReviveMarker* Marker = nullptr;
			for (TActorIterator<AFPSRLReviveMarker> It(World); It; ++It)
			{
				Marker = *It;
			}
			const float Limit = UFPSRLRunSettings::Get().ReviveInterruptDamage;

			++S->Step;
			if (S->Step < 2000)
			{
				S->Step = 2000;
				// Players are only downable in a run; this test runs in the Lobby, so make both downable as a run would.
				for (APawn* Pawn : { HostPawn, Other })
				{
					if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn->GetPlayerState()))
					{
						ASC->AddLooseGameplayTag(FPSRLGameplayTags::Status_Downable, 1, EGameplayTagReplicationState::TagOnly);
					}
				}
				OtherHealth->ApplyEnvironmentDamage(OtherHealth->GetCurrentHealth() + 10.f);	// down the client
				return true;
			}
			switch (S->Step)
			{
			case 2004:
				Check(OtherHealth->IsDowned() && Marker, FString::Printf(TEXT("client downed: %d, revive marker: %d"), OtherHealth->IsDowned(), Marker != nullptr));
				if (const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Other->GetRootComponent()))
				{
					Check(Body->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == ECR_Ignore, TEXT("the downed body lets projectiles through (shots reach the reviver)"));
				}
				if (Marker)
				{
					HostPawn->TeleportTo(Marker->GetActorLocation() + FVector(80.f, 0.f, 0.f), HostPawn->GetActorRotation());
				}
				break;
			case 2006:
				Check(Marker && Marker->TryStartRevive(Host) && Marker->GetReviver() == Host->PlayerState, TEXT("host starts reviving"));
				HostHealth->ApplyEnvironmentDamage(Limit * 0.5f);
				break;
			case 2007:
				Check(Marker && Marker->GetReviver() == Host->PlayerState, FString::Printf(TEXT("%.0f damage (limit %.0f): %s"), Limit * 0.5f, Limit, Marker && Marker->GetReviver() ? TEXT("still reviving") : TEXT("interrupted")));
				HostHealth->ApplyEnvironmentDamage(Limit * 0.5f + 5.f);
				break;
			case 2008:
				Check(Marker && !Marker->GetReviver() && OtherHealth->IsDowned(), FString::Printf(TEXT("%.0f damage in total: %s (expect interrupted, still downed)"), Limit + 5.f, Marker && Marker->GetReviver() ? TEXT("still reviving") : TEXT("interrupted")));
				Host->ServerTestCommand(TEXT("Heal"), FString(), FString());	// the host lost health in the test
				break;
			default:
				break;
			}
			if (S->Step == 2010)
			{
				Check(Marker && Marker->TryStartRevive(Host), TEXT("host starts again"));
				HostHealth->ApplyEnvironmentDamage(Limit * 0.75f);	// the count starts from zero again: under the limit
			}
			const int32 DoneStep = 2012 + FMath::CeilToInt(UFPSRLRunSettings::Get().ReviveSeconds / 0.25f) + 2;
			if (S->Step == DoneStep)
			{
				Check(!OtherHealth->IsDowned() && OtherHealth->GetCurrentHealth() > 0.f, FString::Printf(TEXT("%.0f damage on the second try, then %.1f s: client revived %d, health %.0f"), Limit * 0.75f, UFPSRLRunSettings::Get().ReviveSeconds, !OtherHealth->IsDowned(), OtherHealth->GetCurrentHealth()));
				if (const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Other->GetRootComponent()))
				{
					Check(Body->GetCollisionResponseToChannel(ECC_GameTraceChannel1) != ECR_Ignore, TEXT("revived: the body blocks projectiles again"));
				}
				UE_LOG(LogFPSRL, Log, TEXT("[ReviveTest] done: %d problem(s)"), S->Problems);
				Host->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 0.25f);
	}));
#endif

#if !UE_BUILD_SHIPPING
// Test only: FPSRL.DownedAnimTest (host of a game with a client). The host is downed, then revived 4 s later: every
// machine shows the fall / writhe / crawl and the stand-up on the host's body ([Downed] in each machine's log).
static FAutoConsoleCommandWithWorld GFPSRLDownedAnimTestCommand(TEXT("FPSRL.DownedAnimTest"),
	TEXT("Test (host with a client): down the host, revive it 4 s later; each machine logs the downed body animation ([Downed])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Step](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			APawn* Host = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* Health = Host ? Host->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!Health || World->GetNumPlayerControllers() < 2)
			{
				return true;	// wait for the client
			}
			++*Step;
			if (*Step == 20)
			{
				// Stand 5 m from the client so its screenshots (FPSRL.TeammateShots) show the whole body and the nameplate.
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					if (*It != Host && It->GetPlayerState())
					{
						const FVector Away = (Host->GetActorLocation() - It->GetActorLocation()).GetSafeNormal2D();
						Host->TeleportTo(It->GetActorLocation() + (Away.IsNearlyZero() ? FVector::ForwardVector : Away) * 500.f, Host->GetActorRotation());
						break;
					}
				}
			}
			if (*Step == 40)
			{
				if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Host->GetPlayerState()))
				{
					ASC->AddLooseGameplayTag(FPSRLGameplayTags::Status_Downable, 1, EGameplayTagReplicationState::TagOnly);
				}
				Health->ApplyEnvironmentDamage(Health->GetCurrentHealth() + 10.f);
				UE_LOG(LogFPSRL, Log, TEXT("[DownedAnimTest] host downed: %d"), Health->IsDowned());
			}
			else if (*Step == 40 + 16)	// ticks at 0.25 s: 4 s later
			{
				Health->Revive(0.5f);
				UE_LOG(LogFPSRL, Log, TEXT("[DownedAnimTest] host revived: %d"), !Health->IsDowned());
			}
			else if (*Step == 40 + 30)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[DownedAnimTest] done"));
				return false;
			}
			return true;
		}), 0.25f);
	}));
#endif

#if !UE_BUILD_SHIPPING
// Test only: FPSRL.TeammateShots [count] (any player, rendered): every second, look at the nearest other player and take
// a screenshot (their body, nameplate, downed animation).
static FAutoConsoleCommandWithWorldAndArgs GFPSRLTeammateShotsCommand(TEXT("FPSRL.TeammateShots"),
	TEXT("Test (rendered): look at the nearest other player once a second and take a screenshot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<int32> Left = MakeShared<int32>(Args.IsEmpty() ? 8 : FCString::Atoi(*Args[0]));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Left](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			const APawn* Self = PC ? PC->GetPawn() : nullptr;
			const APawn* Other = nullptr;
			for (TActorIterator<APawn> It(World); It && Self; ++It)
			{
				if (*It != Self && It->GetPlayerState() && (!Other || FVector::Dist(It->GetActorLocation(), Self->GetActorLocation()) < FVector::Dist(Other->GetActorLocation(), Self->GetActorLocation())))
				{
					Other = *It;
				}
			}
			if (!Other)
			{
				return true;
			}
			PC->SetControlRotation((Other->GetActorLocation() - FVector(0.f, 0.f, 60.f) - Self->GetActorLocation()).Rotation());
			PC->ConsoleCommand(TEXT("Shot showui"));
			UE_LOG(LogFPSRL, Log, TEXT("[TeammateShots] %s at %.0f cm"), *Other->GetName(), FVector::Dist(Other->GetActorLocation(), Self->GetActorLocation()));
			return --*Left > 0;
		}), 1.f);
	}));
#endif
