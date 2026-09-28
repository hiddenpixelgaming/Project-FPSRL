// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLAutopilotSubsystem.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Combat/FPSRLWeapon.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Rooms/FPSRLExitPortal.h"
#include "Rooms/FPSRLRoom.h"
#include "FPSRL.h"

void UFPSRLAutopilotSubsystem::Start(bool bTestFall, int32 InMinPlayers)
{
#if !UE_BUILD_SHIPPING
	bFallPending = bTestFall;
	MinPlayers = FMath::Max(1, InMinPlayers);
	if (!TickerHandle.IsValid())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] started"));
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Step), 1.f);
	}
#endif
}

void UFPSRLAutopilotSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

void UFPSRLAutopilotSubsystem::Finish(const TCHAR* Result)
{
	UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] finished: %s after %d steps"), Result, Steps);
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}

bool UFPSRLAutopilotSubsystem::Step(float DeltaTime)
{
	UWorld* World = GetGameInstance()->GetWorld();
	UFPSRLRunSubsystem* Run = GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>();
	if (++Steps > MaxSteps)
	{
		Finish(TEXT("TIMED OUT"));
		return false;
	}
	if (!World || !Run || World->IsInSeamlessTravel())
	{
		return true;
	}

	if (!Run->IsRunActive())
	{
		if (!bStartedRun)
		{
			const AGameStateBase* LobbyState = World->GetGameState();
			if (!LobbyState || LobbyState->PlayerArray.Num() < MinPlayers)
			{
				return true;	// waiting for the other test players to join
			}
			if (bAltarTest)
			{
				RunAltarTest();
				Finish(TEXT("altar test done"));
				return false;
			}
			bStartedRun = Run->StartRun(World);
			UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] start run: %s"), bStartedRun ? TEXT("ok") : TEXT("FAILED"));
		}
		else if (!World->GetGameState<AFPSRLGameState>())
		{
			Finish(TEXT("run over, back in the Lobby"));
			return false;
		}
		return true;
	}

	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(World);
	AFPSRLGameState* GameState = World->GetGameState<AFPSRLGameState>();
	UFPSRLDepthLayoutComponent* Layout = GameState ? GameState->DepthLayout.Get() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Layout || !Layout->HasLayout() || !Pawn)
	{
		return true;
	}

	// First encounter not yet cleared.
	int32 Next = INDEX_NONE;
	for (int32 Index = 0; Index < Layout->Placements.Num(); ++Index)
	{
		const UFPSRLRoomDefinition* Room = Layout->Placements[Index].Room;
		if (Room && UFPSRLDepthLayoutComponent::IsEncounterRoom(Room->RoomType) && !Layout->EncounterCleared[Index])
		{
			Next = Index;
			break;
		}
	}

	FString States;
	for (const EFPSRLRoomState State : Layout->RoomStates)
	{
		States += FString::Printf(TEXT("%d"), static_cast<int32>(State));
	}
	const FString Status = FString::Printf(TEXT("Depth %d: state=%s boss=%s levelComplete=%d window=%d-%d ready=%d cleared=%d/%d next=%d rooms=[%s]"),
		Run->GetDepthNumber(), *UEnum::GetDisplayValueAsText(GameState->DepthState).ToString(),
		*UEnum::GetDisplayValueAsText(GameState->FinalLevelBossState).ToString(), GameState->bLevelComplete ? 1 : 0,
		Layout->Window.First, Layout->Window.Last, Layout->ReadyThrough, Layout->GetClearedEncounterCount(),
		Layout->GetRequiredEncounterCount(), Next, *States);
	if (Status != LastStatus)
	{
		LastStatus = Status;
		UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] %s"), *Status);
	}

	// Depth done: go to the portal and vote to continue.
	for (TActorIterator<AFPSRLExitPortal> It(World); It; ++It)
	{
		if (It->CanInteract())
		{
			if (!It->HasVotedToContinue(PC->PlayerState))
			{
				Pawn->TeleportTo(It->GetActorLocation() - FVector(250.f, 0.f, 0.f), Pawn->GetActorRotation());
				UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] voting at %s"), *It->GetActorNameOrLabel());
				It->SetContinueVote(PC, true);
			}
			return true;
		}
	}

	if (Next == INDEX_NONE || Layout->ReadyThrough < Next)
	{
		return true;	// waiting for the next encounter to load everywhere, or for the Depth to complete
	}

	const FVector Target = Layout->Placements[Next].Transform.TransformPosition(FVector(250.f, 0.f, 120.f));
	if (FVector::Dist2D(Pawn->GetActorLocation(), Target) > 200.f)
	{
		Pawn->TeleportTo(Target, Layout->Placements[Next].Transform.Rotator());
		UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] moved into room %d (%s)"), Next, *GetNameSafe(Layout->Placements[Next].Room));
		return true;
	}

	if (bFallPending)
	{
		bFallPending = false;
		const FVector Off = Target + Layout->Placements[Next].Transform.TransformVector(FVector(0.f, 3000.f, 0.f)) - FVector(0.f, 0.f, 1100.f);
		Pawn->TeleportTo(Off, Pawn->GetActorRotation(), false, true);
		UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] dropped off the level at %s"), *Off.ToCompactString());
		return true;
	}

	AFPSRLRoom* RoomActor = nullptr;
	for (TActorIterator<AFPSRLRoom> It(World); It && !RoomActor; ++It)
	{
		RoomActor = Layout->FindPlacementIndex(*It) == Next ? *It : nullptr;
	}
	if (!RoomActor)
	{
		return true;
	}
	if (!RoomActor->bCombatStarted)
	{
		RoomActor->StartCombat();
		return true;
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		UFPSRLHealthComponent* Health = It->FindComponentByClass<UFPSRLHealthComponent>();
		if (!It->IsPlayerControlled() && FVector::Dist(It->GetActorLocation(), RoomActor->GetActorLocation()) < 2500.f && Health && !Health->IsDead())
		{
			UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] killing %s in room %d (max health %.0f, scale %.2f)"), *It->GetName(), Next,
				Health->GetMaxHealth(), It->GetActorScale3D().X);
			Health->Kill();
		}
	}
	return true;
}

void UFPSRLAutopilotSubsystem::StartAltarTest(int32 InMinPlayers)
{
	bAltarTest = true;
	Start(false, InMinPlayers);
}

void UFPSRLAutopilotSubsystem::RunAltarTest()
{
	const AGameStateBase* GameState = GetGameInstance()->GetWorld()->GetGameState();
	TArray<UFPSRLBoonComponent*> Players;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (const AFPSRLPlayerState* PS = Cast<AFPSRLPlayerState>(PlayerState))
		{
			Players.Add(PS->GetBoonComponent());
		}
	}
	auto SetOf = [](const UFPSRLBoonComponent* Boons)
	{
		TArray<FString> Names;
		for (const UFPSRLAspectDefinition* Aspect : Boons->AspectOptions)
		{
			Names.Add(GetNameSafe(Aspect).Replace(TEXT("DA_Aspect_"), TEXT("")));
		}
		Names.Sort();
		return FString::Join(Names, TEXT(","));
	};

	int32 SameAsTeammate = 0, Rerolls = 0, RerollSame = 0, RerollMirror = 0;
	TMap<FString, int32> RerollResults;
	for (int32 Trial = 0; Trial < 40; ++Trial)
	{
		for (UFPSRLBoonComponent* Boons : Players)
		{
			Boons->TestCancelSelection();
		}
		TArray<FString> Sets;
		for (UFPSRLBoonComponent* Boons : Players)
		{
			Boons->BeginAltar();
			Sets.Add(SetOf(Boons));
		}
		for (int32 A = 0; A < Sets.Num(); ++A)
		{
			for (int32 B = A + 1; B < Sets.Num(); ++B)
			{
				SameAsTeammate += Sets[A] == Sets[B] ? 1 : 0;
			}
		}
		if (Trial < 3)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[AltarTest] trial %d: %s"), Trial, *FString::Join(Sets, TEXT("  vs  ")));
		}
		// Rerolls on the first player: never the same set, and not always the mirror ("the other three").
		UFPSRLBoonComponent* First = Players[0];
		for (int32 Reroll = 0; Reroll < 3; ++Reroll)
		{
			const FString Before = SetOf(First);
			if (!First->TryReroll(First->SelectionEventId))
			{
				break;
			}
			const FString After = SetOf(First);
			++Rerolls;
			RerollSame += After == Before ? 1 : 0;
			TArray<FString> BeforeNames;
			Before.ParseIntoArray(BeforeNames, TEXT(","));
			bool bDisjoint = true;
			for (const FString& Name : BeforeNames)
			{
				bDisjoint &= !After.Contains(Name);
			}
			RerollMirror += bDisjoint ? 1 : 0;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[AltarTest] %d players, 40 altars: identical teammate sets %d; %d rerolls: same set %d, fully different set %d"),
		Players.Num(), SameAsTeammate, Rerolls, RerollSame, RerollMirror);
}

void UFPSRLAutopilotSubsystem::StartMeleeTest()
{
#if !UE_BUILD_SHIPPING
	MeleeStep = 0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::StepMeleeTest), 1.f);
	UE_LOG(LogFPSRL, Log, TEXT("[MeleeTest] started"));
#endif
}

bool UFPSRLAutopilotSubsystem::StepMeleeTest(float DeltaTime)
{
	UWorld* World = GetGameInstance()->GetWorld();
	AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GetGameInstance()->GetFirstLocalPlayerController(World)) : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const bool bInGame = World && (World->GetNetMode() == NM_Client ? PC && PC->GetNetConnection() != nullptr : World->GetGameState() != nullptr);
	if (!Pawn || !bInGame)
	{
		return ++MeleeStep < 120;	// waiting to be in the host's game with a pawn
	}
	if (MeleeStep < 1000)
	{
		MeleeStep = 1000;
		PC->ServerTestCommand(TEXT("God"), FString(), FString());
		PC->ServerTestCommand(TEXT("SpawnTestEnemy"), FString(), FString());
		return true;
	}
	if (++MeleeStep <= 1004)
	{
		if (MeleeStep >= 1002)	// give the enemy time to replicate, then swing (the Blueprint's own melee)
		{
			const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
			UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			if (UInputAction* Melee = LoadObject<UInputAction>(nullptr, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee")); Melee && Input)
			{
				Input->InjectInputForAction(Melee, FInputActionValue(true), {}, {});	// a real key press, as far as every binding is concerned
				UE_LOG(LogFPSRL, Log, TEXT("[MeleeTest] melee pressed"));
			}
		}
		return true;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[MeleeTest] done"));
	PC->ConsoleCommand(TEXT("quit"));
	return false;
}

static FAutoConsoleCommandWithWorld GFPSRLMeleeTestCommand(TEXT("FPSRL.MeleeTest"),
	TEXT("Client test: spawn an enemy in front and melee it 3 times (see the host log)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			World->GetGameInstance()->GetSubsystem<UFPSRLAutopilotSubsystem>()->StartMeleeTest();
		}
	}));

void UFPSRLAutopilotSubsystem::StartWeaponTest()
{
#if !UE_BUILD_SHIPPING
	WeaponStep = 0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::StepWeaponTest), 0.25f);
	UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] started"));
#endif
}

bool UFPSRLAutopilotSubsystem::StepWeaponTest(float DeltaTime)
{
	UWorld* World = GetGameInstance()->GetWorld();
	AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GetGameInstance()->GetFirstLocalPlayerController(World)) : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !World->GetGameState())
	{
		return ++WeaponStep < 400;
	}
	auto FindWeapon = [World, Pawn]() -> AFPSRLWeapon*
	{
		for (TActorIterator<AFPSRLWeapon> It(World); It; ++It)
		{
			if (It->GetOwner() == Pawn && !It->IsHidden())
			{
				return *It;
			}
		}
		return nullptr;
	};
	auto EnemyHealth = [World]() -> double
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (!It->IsPlayerControlled())
			{
				if (const UFPSRLHealthComponent* Health = It->FindComponentByClass<UFPSRLHealthComponent>())
				{
					return Health->GetCurrentHealth();
				}
			}
		}
		return -1.0;
	};

	++WeaponStep;
	if (WeaponStep < 1000)
	{
		WeaponStep = 1000;
		PC->ServerTestCommand(TEXT("God"), FString(), FString());
		UClass* Rifle = LoadClass<AActor>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
		const bool bGiven = AFPSRLPlayerController::GiveWeaponToPawn(Pawn, Rifle);
		PC->ServerTestCommand(TEXT("SpawnTestEnemy"), FString(), FString());
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] rifle given: %s"), bGiven ? TEXT("yes") : TEXT("NO"));
		return true;
	}
	AFPSRLWeapon* Weapon = FindWeapon();
	if (!Weapon)
	{
		UE_LOG(LogFPSRL, Error, TEXT("[WeaponTest] FAILED: the player holds no C++ weapon"));
		PC->ConsoleCommand(TEXT("quit"));
		return false;
	}
	switch (WeaponStep)
	{
	case 1004:	// one real trigger press (IA_Shoot through the character Blueprint)
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] holding %s: %d/%d rounds, enemy health %.0f"), *Weapon->GetClass()->GetName(),
			Weapon->GetCurrentBullets(), Weapon->GetMagSize(), EnemyHealth());
		if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (UInputAction* Shoot = LoadObject<UInputAction>(nullptr, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot")))
				{
					Input->InjectInputForAction(Shoot, FInputActionValue(true), {}, {});
				}
			}
		}
		break;
	case 1008:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after one press: %d rounds, enemy health %.0f, firing %d"), Weapon->GetCurrentBullets(), EnemyHealth(), Weapon->IsWeaponFiring() ? 1 : 0);
		Weapon->StopFiring();
		Weapon->StartFiring();	// hold the trigger for a second (full auto)
		break;
	case 1012:
		Weapon->StopFiring();
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after 1 s full auto: %d rounds (rifle fires every %.2f s), spread %.1f"), Weapon->GetCurrentBullets(), Weapon->GetRefireRate(), Weapon->GetAimVariance());
		Weapon->StartAiming();
		break;
	case 1014:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] aiming: %d, refire %.2f s; holding until empty"), Weapon->IsWeaponAiming() ? 1 : 0, Weapon->GetRefireRate());
		Weapon->StopAiming();
		Weapon->StartFiring();
		break;
	case 1030:
		Weapon->StopFiring();
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after emptying: %d/%d rounds (reloaded: %s), enemy health %.0f"), Weapon->GetCurrentBullets(), Weapon->GetMagSize(),
			Weapon->GetCurrentBullets() == Weapon->GetMagSize() || Weapon->GetCurrentBullets() > 30 ? TEXT("yes") : TEXT("NO"), EnemyHealth());
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] done"));
		PC->ConsoleCommand(TEXT("quit"));
		return false;
	default:
		break;
	}
	return true;
}

static FAutoConsoleCommandWithWorld GFPSRLWeaponTestCommand(TEXT("FPSRL.WeaponTest"),
	TEXT("Host test: rifle, enemy in front, shoot / full auto / aim / empty + reload (see [WeaponTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			World->GetGameInstance()->GetSubsystem<UFPSRLAutopilotSubsystem>()->StartWeaponTest();
		}
	}));
