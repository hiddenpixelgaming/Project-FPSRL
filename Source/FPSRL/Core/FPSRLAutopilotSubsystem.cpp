// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLAutopilotSubsystem.h"
#include "Components/FPSRLBoonComponent.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "Components/FPSRLHealthComponent.h"
#include "Combat/FPSRLWeapon.h"
#include "UI/FPSRLCombatHUDWidget.h"
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
#include "AI/FPSRLEnemyAIController.h"
#include "HAL/IConsoleManager.h"

namespace FPSRLAutopilot
{
	static TAutoConsoleVariable<FString> CVarShots(TEXT("fpsrl.Autopilot.Shots"), TEXT(""),
		TEXT("Autopilot: screenshot viewpoints in the first combat room, relative to its room actor: 'x y z yaw pitch;...'."));

	static TAutoConsoleVariable<float> CVarFightSeconds(TEXT("fpsrl.Autopilot.FightSeconds"), 0.f,
		TEXT("Autopilot: seconds each encounter's enemies fight (host in god mode) before being killed (0 = at once)."));
	static TAutoConsoleVariable<bool> CVarMoveParty(TEXT("fpsrl.Autopilot.MoveParty"), false,
		TEXT("Autopilot: move every player into each room with the host (default: only the host; the others stay behind)."));

	static TAutoConsoleVariable<FString> CVarShotsAfterClear(TEXT("fpsrl.Autopilot.ShotsAfterClear"), TEXT(""),
		TEXT("Autopilot: screenshot viewpoints in the first room fought, once it is cleared (same format as Shots): what it leaves behind."));
	static TAutoConsoleVariable<bool> CVarShotEveryRoom(TEXT("fpsrl.Autopilot.ShotEveryRoom"), false,
		TEXT("Autopilot: screenshot every room of each Depth from its entrance as it loads (Room_D<depth>_<index>_<room>)."));

}
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

	// Room review: every room of the Depth from its entrance, in order, as soon as it is loaded (before its fight).
	const bool bShootRooms = FPSRLAutopilot::CVarShotEveryRoom.GetValueOnGameThread();
	if (bShootRooms)
	{
		if (RoomShotDepth != Run->GetDepthNumber())
		{
			RoomShotDepth = Run->GetDepthNumber();
			LastRoomShot = INDEX_NONE;
		}
		const int32 Through = Next == INDEX_NONE ? Layout->Placements.Num() - 1 : Next;
		if (LastRoomShot < Through && ShootRoom(Layout, PC, Pawn, LastRoomShot + 1))
		{
			return true;
		}
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

	// Review of what a cleared room leaves (its reward by the exit): screenshots in the first room fought, after its clear.
	const FString AfterClearShots = FPSRLAutopilot::CVarShotsAfterClear.GetValueOnGameThread();
	if (!AfterClearShots.IsEmpty() && AfterClearShotIndex != -2 && Layout->EncounterCleared.IsValidIndex(FightRoom) && Layout->EncounterCleared[FightRoom])
	{
		AFPSRLRoom* Cleared = nullptr;
		for (TActorIterator<AFPSRLRoom> It(World); It && !Cleared; ++It)
		{
			Cleared = Layout->FindPlacementIndex(*It) == FightRoom ? *It : nullptr;
		}
		TArray<FString> Views;
		AfterClearShots.ParseIntoArray(Views, TEXT(";"));
		AfterClearShotIndex = FMath::Max(AfterClearShotIndex, 0);
		if (Cleared && Views.IsValidIndex(AfterClearShotIndex))
		{
			TArray<FString> Parts;
			Views[AfterClearShotIndex].ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 5)
			{
				const FVector Spot = Cleared->GetActorTransform().TransformPositionNoScale(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
				const FRotator View(FCString::Atof(*Parts[4]), Cleared->GetActorRotation().Yaw + FCString::Atof(*Parts[3]), 0.f);
				Pawn->TeleportTo(Spot, FRotator(0.f, View.Yaw, 0.f), false, true);
				PC->SetControlRotation(View);
				PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1600x900 filename=Cleared_%s_%02d"), *Cleared->GetLevel()->GetOuter()->GetName(), AfterClearShotIndex));
				UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] cleared-room shot %d at %s"), AfterClearShotIndex, *Spot.ToCompactString());
			}
			++AfterClearShotIndex;
			return true;
		}
		AfterClearShotIndex = -2;	// done
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
		if (FPSRLAutopilot::CVarMoveParty.GetValueOnGameThread())
		{
			int32 Slot = 1;
			for (FConstPlayerControllerIterator It = Pawn->GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APawn* Other = It->Get() ? It->Get()->GetPawn() : nullptr; Other && Other != Pawn)
				{
					const FVector Beside = Layout->Placements[Next].Transform.TransformPosition(FVector(250.f, (Slot % 2 ? 1.f : -1.f) * 130.f * ((Slot + 1) / 2), 120.f));
					Other->TeleportTo(Beside, Layout->Placements[Next].Transform.Rotator());
					++Slot;
				}
			}
		}
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
		// Arena review: screenshots from viewpoints given relative to the room actor ("x y z yaw pitch;..."), once.
		const FString Shots = FPSRLAutopilot::CVarShots.GetValueOnGameThread();
		if (!Shots.IsEmpty() && ShotIndex != -2)
		{
			TArray<FString> Views;
			Shots.ParseIntoArray(Views, TEXT(";"));
			ShotIndex = FMath::Max(ShotIndex, 0);
			if (Views.IsValidIndex(ShotIndex))
			{
				TArray<FString> Parts;
				Views[ShotIndex].ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 5 && Pawn)
				{
					const FVector Local(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
					const FVector Spot = RoomActor->GetActorTransform().TransformPositionNoScale(Local);
					const FRotator View(FCString::Atof(*Parts[4]), RoomActor->GetActorRotation().Yaw + FCString::Atof(*Parts[3]), 0.f);
					Pawn->TeleportTo(Spot, FRotator(0.f, View.Yaw, 0.f), false, true);
					PC->SetControlRotation(View);
					PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1600x900 filename=Arena_%s_%02d"), *RoomActor->GetLevel()->GetOuter()->GetName(), ShotIndex));
					UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] arena shot %d at %s"), ShotIndex, *Spot.ToCompactString());
				}
				++ShotIndex;
				return true;
			}
			ShotIndex = -2;	// done
		}
		RoomActor->StartCombat();
		FightStartTime = World->GetTimeSeconds();
		FightRoom = Next;
		if (FPSRLAutopilot::CVarFightSeconds.GetValueOnGameThread() > 0.f && !bFightGodSet)
		{
			bFightGodSet = true;
			if (AFPSRLPlayerController* FightPC = Cast<AFPSRLPlayerController>(PC))
			{
				FightPC->ServerTestCommand(TEXT("God"), FString(), FString());	// watch the AI fight without losing the run
			}
		}
		return true;
	}
	// Diagnosis: a living enemy the autopilot can't reach (stuck far from its room) keeps the room from ever clearing.
	if (FightRoom == Next && World->GetTimeSeconds() - FightStartTime > FPSRLAutopilot::CVarFightSeconds.GetValueOnGameThread() + 20.0
		&& World->GetTimeSeconds() - LastStrayReport > 10.0)
	{
		LastStrayReport = World->GetTimeSeconds();
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const UFPSRLHealthComponent* Health = It->FindComponentByClass<UFPSRLHealthComponent>();
			const AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(It->GetController());
			if (AI && Health && !Health->IsDead() && FVector::Dist(It->GetActorLocation(), RoomActor->GetActorLocation()) >= 4500.f)
			{
				UE_LOG(LogFPSRL, Warning, TEXT("[Autopilot] out of reach in room %d at %s: %s, path failures %d"), Next,
					*It->GetActorLocation().ToCompactString(), *AI->Describe(), AI->GetMoveFailures());
			}
		}
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		UFPSRLHealthComponent* Health = It->FindComponentByClass<UFPSRLHealthComponent>();
		if (!It->IsPlayerControlled() && FVector::Dist(It->GetActorLocation(), RoomActor->GetActorLocation()) < 4500.f && Health && !Health->IsDead())
		{
			if (FightRoom == Next && World->GetTimeSeconds() - FightStartTime < FPSRLAutopilot::CVarFightSeconds.GetValueOnGameThread())
			{
				continue;	// still fighting
			}
			if (const AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(It->GetController()))
			{
				UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] AI before the kill: %s, path failures %d"), *AI->Describe(), AI->GetMoveFailures());
			}
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
		if (WeaponStep > 1050)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] done (the player went down on purpose; solo = party wipe)"));
			PC->ConsoleCommand(TEXT("quit"));
			return false;
		}
		UE_LOG(LogFPSRL, Error, TEXT("[WeaponTest] FAILED: the player holds no C++ weapon"));
		PC->ConsoleCommand(TEXT("quit"));
		return false;
	}
	auto HUDState = [PC]() { return PC->GetCombatHUD() ? PC->GetCombatHUD()->DescribeForTest() : FString(TEXT("no HUD")); };
	auto Press = [PC](const TCHAR* ActionPath)
	{
		const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		if (UInputAction* Action = LoadObject<UInputAction>(nullptr, ActionPath); Action && Input)
		{
			Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});	// a real key press for every binding
		}
	};
	switch (WeaponStep)	// 4 steps per second
	{
	case 1004:	// one real trigger press (IA_Shoot through the character Blueprint)
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] holding %s: %d/%d rounds, enemy health %.0f"), *Weapon->GetClass()->GetName(),
			Weapon->GetCurrentBullets(), Weapon->GetMagSize(), EnemyHealth());
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] HUD: %s"), *HUDState());
		Press(TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot"));
		break;
	case 1008:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after one press: %d rounds, enemy health %.0f"), Weapon->GetCurrentBullets(), EnemyHealth());
		Weapon->StopFiring();
		Weapon->StartFiring();	// hold the trigger for a second (full auto)
		break;
	case 1012:
		Weapon->StopFiring();
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after 1 s full auto: %d rounds, spread %.1f; HUD: %s"), Weapon->GetCurrentBullets(), Weapon->GetAimVariance(), *HUDState());
		Weapon->StartFiring();	// hold until empty (29 rounds at 0.1 s)
		break;
	case 1026:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] emptied, reloading %d (%.1f s): %d rounds; HUD: %s"), Weapon->IsReloading() ? 1 : 0, Weapon->ReloadDuration, Weapon->GetCurrentBullets(), *HUDState());
		break;
	case 1029:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] mid reload: HUD: %s"), *HUDState());
		break;
	case 1034:
		Weapon->StopFiring();
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] after reload: %d/%d rounds, reloading %d; HUD: %s"), Weapon->GetCurrentBullets(), Weapon->GetMagSize(), Weapon->IsReloading() ? 1 : 0, *HUDState());
		PC->ServerTestCommand(TEXT("SpawnTestEnemy"), FString(), FString());	// a fresh target for melee
		break;
	case 1038:
		Press(TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee"));
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] melee pressed"));
		break;
	case 1039:
		Press(TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee"));	// 0.25 s later: must be ignored (1 s cooldown)
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] melee pressed again at 0.25 s; HUD: %s"), *HUDState());
		break;
	case 1041:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] melee at 0.75 s; HUD: %s"), *HUDState());
		break;
	case 1043:
		Press(TEXT("/Game/Variant_Shooter/Input/Actions/IA_Melee.IA_Melee"));	// 1.25 s: allowed again
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] melee pressed at 1.25 s"));
		break;
	case 1044:
		Press(TEXT("/Game/Variant_Shooter/Input/Actions/IA_Dash.IA_Dash"));
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] dash pressed"));
		break;
	case 1045:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] 0.25 s after dash: HUD: %s"), *HUDState());
		PC->ServerTestCommand(TEXT("God"), FString(), FString());	// god mode off
		break;
	case 1046:
		if (UFPSRLHealthComponent* Health = Pawn->FindComponentByClass<UFPSRLHealthComponent>())
		{
			Health->ApplyEnvironmentDamage(10.f);
		}
		break;
	case 1047:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] 0.25 s after taking 10 damage: HUD: %s"), *HUDState());
		break;
	case 1050:
		UE_LOG(LogFPSRL, Log, TEXT("[WeaponTest] 1 s after: HUD: %s"), *HUDState());
		if (UFPSRLHealthComponent* Health = Pawn->FindComponentByClass<UFPSRLHealthComponent>())
		{
			Health->ApplyEnvironmentDamage(Health->GetCurrentHealth() + 10.f);	// goes down (purple)
		}
		break;
	case 1054:
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

static FAutoConsoleCommandWithWorldAndArgs GFPSRLHurtCommand(TEXT("FPSRL.HurtMe"),
	TEXT("Test: once in a game, ask the server to damage this player by the given amount (default 1000), then quit after 4 s."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const FString Amount = Args.Num() > 0 ? Args[0] : FString(TEXT("1000"));
		TWeakObjectPtr<UGameInstance> GameInstance = World ? World->GetGameInstance() : nullptr;
		TSharedRef<int32> Step = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Amount, Step](float)
		{
			UGameInstance* GI = GameInstance.Get();
			AFPSRLPlayerController* PC = GI ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
			if (!PC || !PC->GetPawn() || (PC->GetNetMode() == NM_Client && !PC->GetNetConnection()) || PC->GetWorld()->GetMapName().Contains(TEXT("Lobby")))
			{
				return ++*Step < 300;	// waiting to be in a Depth
			}
			if (*Step < 1000)
			{
				*Step = 1000;
				PC->ServerTestCommand(TEXT("Hurt"), Amount, FString());
				return true;
			}
			if (++*Step > 1004)
			{
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			return true;
		}), 1.f);
	}));

// Host test: fill Primary with Fire and Secondary with Water through the real altar flow, one position at a time, and
// check each position offers the right kind (3rd Minor, 6th Major, others Normal) and only source-compatible Blessings.
static FAutoConsoleCommandWithWorld GFPSRLProgressionTestCommand(TEXT("FPSRL.ProgressionTest"),
	TEXT("Host test: Blessing positions and source compatibility through the altar ([ProgressionTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
		if (!Boons)
		{
			UE_LOG(LogFPSRL, Error, TEXT("[ProgressionTest] no player"));
			return;
		}
		const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
		int32 Problems = 0;
		const TPair<EFPSRLBoonChannel, const TCHAR*> Plan[] = { { EFPSRLBoonChannel::Primary, TEXT("DA_Aspect_Fire") }, { EFPSRLBoonChannel::Secondary, TEXT("DA_Aspect_Water") } };
		for (const TPair<EFPSRLBoonChannel, const TCHAR*>& Step : Plan)
		{
			const EFPSRLBoonChannel Channel = Step.Key;
			const EFPSRLItemSource Source = Boons->GetChannelSource(Channel);
			for (int32 Position = 1; Position <= Settings.MaxBoonsPerChannel; ++Position)
			{
				Boons->TestCancelSelection();
				Boons->BeginAltar();
				int32 AspectIndex = INDEX_NONE;
				for (int32 Try = 0; Try < 40 && AspectIndex == INDEX_NONE; ++Try)
				{
					AspectIndex = Boons->AspectOptions.IndexOfByPredicate([&](const UFPSRLAspectDefinition* A) { return A && A->GetName() == Step.Value; });
					if (AspectIndex == INDEX_NONE)
					{
						Boons->TryReroll(Boons->SelectionEventId);
					}
				}
				if (AspectIndex == INDEX_NONE || !Boons->TryChooseAspect(Boons->SelectionEventId, AspectIndex))
				{
					UE_LOG(LogFPSRL, Log, TEXT("[ProgressionTest] %s position %d: %s no longer offered (no compatible Blessing of the right kind left)"), *UFPSRLBoonComponent::GetChannelName(Channel).ToString(), Position, Step.Value);
					break;
				}
				if (Boons->AltarStep == EFPSRLAltarStep::ChooseSlot)
				{
					Boons->TryChooseSlot(Boons->SelectionEventId, Boons->SlotOptions.IndexOfByKey(Channel));
				}
				const EFPSRLBoonType Wanted = Settings.GetTypeForPosition(Position);
				FString Line;
				bool bOk = !Boons->CurrentOptions.IsEmpty();
				for (const FFPSRLBoonOffer& Offer : Boons->CurrentOptions)
				{
					const bool bKind = Offer.Boon && Offer.Boon->BoonType == Wanted;
					const bool bSource = Offer.Boon && Offer.Boon->SupportsSource(Source) && Offer.Channel == Channel;
					bOk &= bKind && bSource;
					FString Sources;
					for (EFPSRLItemSource S : Offer.Boon->SupportedSources)
					{
						Sources += UEnum::GetDisplayValueAsText(S).ToString() + TEXT(" ");
					}
					Line += FString::Printf(TEXT("%s(%s, %s) "), *GetNameSafe(Offer.Boon).Replace(TEXT("DA_Boon_"), TEXT("")),
						*UEnum::GetDisplayValueAsText(Offer.Boon->BoonType).ToString(), Sources.IsEmpty() ? TEXT("Universal") : *Sources.TrimEnd());
				}
				Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[ProgressionTest] %s (%s) pos %2d wants %-6s: %s%s"), *UFPSRLBoonComponent::GetChannelName(Channel).ToString(),
					*UEnum::GetDisplayValueAsText(Source).ToString(), Position, *UEnum::GetDisplayValueAsText(Wanted).ToString(), *Line, bOk ? TEXT("OK") : TEXT("<-- WRONG"));
				Boons->TrySelect(Boons->SelectionEventId, 0);
			}
		}
		UE_LOG(LogFPSRL, Log, TEXT("[ProgressionTest] done: %d problem(s)"), Problems);
	}));

bool UFPSRLAutopilotSubsystem::ShootRoom(UFPSRLDepthLayoutComponent* Layout, APlayerController* PC, APawn* Pawn, int32 Index)
{
	if (!Layout || !PC || !Pawn || !Layout->Placements.IsValidIndex(Index) || !Layout->Window.Contains(Index) || Layout->ReadyThrough < Index)
	{
		return false;	// not loaded yet: try again next step
	}
	const FTransform& Room = Layout->Placements[Index].Transform;
	const FVector Spot = Room.TransformPosition(FVector(250.f, 0.f, 160.f));
	const FRotator View(-5.f, Room.Rotator().Yaw, 0.f);
	if (FVector::Dist2D(Pawn->GetActorLocation(), Spot) > 50.f)	// 2D: the pawn drops onto the floor
	{
		// Go there first and let the camera's auto exposure settle (3 steps), so the shot shows the room as a player sees it.
		Pawn->TeleportTo(Spot, FRotator(0.f, View.Yaw, 0.f), false, true);
		PC->SetControlRotation(View);
		RoomShotSettle = 3;
		return true;
	}
	if (--RoomShotSettle > 0)
	{
		return true;
	}
	PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1600x900 filename=Room_D%d_%02d_%s"), RoomShotDepth, Index, *GetNameSafe(Layout->Placements[Index].Room)));
	UE_LOG(LogFPSRL, Log, TEXT("[Autopilot] room shot %d (%s) at %s"), Index, *GetNameSafe(Layout->Placements[Index].Room), *Spot.ToCompactString());
	LastRoomShot = Index;
	return true;
}
