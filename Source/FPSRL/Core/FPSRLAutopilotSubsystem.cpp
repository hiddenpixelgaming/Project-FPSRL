// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLAutopilotSubsystem.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLGameState.h"
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

void UFPSRLAutopilotSubsystem::Start()
{
#if !UE_BUILD_SHIPPING
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
