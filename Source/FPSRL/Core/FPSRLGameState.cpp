// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLGameState.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/GameInstance.h"
#include "Rooms/FPSRLRoom.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "FPSRL.h"

void AFPSRLGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLGameState, AreaNumber);
	DOREPLIFETIME(AFPSRLGameState, DepthNumber);
	DOREPLIFETIME(AFPSRLGameState, RequiredRooms);
	DOREPLIFETIME(AFPSRLGameState, CompletedRooms);
	DOREPLIFETIME(AFPSRLGameState, bDepthComplete);
	DOREPLIFETIME(AFPSRLGameState, DepthState);
	DOREPLIFETIME(AFPSRLGameState, FinalLevelBossState);
	DOREPLIFETIME(AFPSRLGameState, bLevelComplete);
}

void AFPSRLGameState::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	const UFPSRLDepthDefinition* Depth = nullptr;
	if (const UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		AreaNumber = RunSubsystem->GetAreaNumber();
		DepthNumber = RunSubsystem->GetDepthNumber();
		Depth = RunSubsystem->GetCurrentDepth();
	}

	// Generated Depth: the rooms stream in one by one; evaluate once the first ones are in.
	DepthLayout->OnLayoutReady.AddUObject(this, &ThisClass::EvaluateEmptyDepth);
	DepthLayout->OnRoomStateChanged.AddUObject(this, &ThisClass::RefreshFinalLevelBossState);
	DepthState = EFPSRLDepthState::Loading;
	if (DepthLayout->BuildLayout(Depth))
	{
		RecountRooms();
		return;
	}

	// Handcrafted Depth: rooms register in their own BeginPlay (same frame). Check on the next tick so a Depth
	// without required rooms (merchant / preparation) completes immediately, and one with rooms waits for them.
	GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::EvaluateEmptyDepth);
}

AFPSRLGameState::AFPSRLGameState()
{
	DepthLayout = CreateDefaultSubobject<UFPSRLDepthLayoutComponent>(TEXT("DepthLayout"));
}

void AFPSRLGameState::EvaluateEmptyDepth()
{
	if (DepthState == EFPSRLDepthState::Loading || DepthState == EFPSRLDepthState::NotStarted)
	{
		DepthState = EFPSRLDepthState::Active;
	}
	RecountRooms();
	UE_LOG(LogFPSRL, Log, TEXT("[Depth] Area %d Depth %d: %d required room(s)"), AreaNumber, DepthNumber, RequiredRooms);
	if (RequiredRooms == 0)
	{
		CompleteDepth();
	}
	else if (CompletedRooms >= RequiredRooms)
	{
		CompleteDepth();	// every room was already cleared (e.g. empty encounters) while streaming in
	}
}

void AFPSRLGameState::RegisterRequiredRoom(AFPSRLRoom* Room)
{
	if (HasAuthority() && Room && !RequiredRoomList.Contains(Room))
	{
		RequiredRoomList.Add(Room);
		RecountRooms();
	}
}

void AFPSRLGameState::NotifyRoomCompleted(AFPSRLRoom* Room)
{
	if (!HasAuthority() || bDepthComplete)
	{
		return;
	}
	DepthLayout->NotifyEncounterCleared(Room);	// remembered even after the room unloads
	RecountRooms();
	if (Room && Room->RoomType == ERoomType::Boss)
	{
		bLevelComplete = true;	// the Final Level Boss is down: the level is complete
		FinalLevelBossState = EFPSRLBossState::Defeated;
		UE_LOG(LogFPSRL, Log, TEXT("[Depth] Final Level Boss defeated: level complete"));
		OnRep_LevelComplete();
	}
	// A generated Depth knows all its encounters up front; a handcrafted one waits until its rooms have registered.
	if ((DepthLayout->HasLayout() || !DepthLayout->IsLayoutPending()) && RequiredRooms > 0 && CompletedRooms >= RequiredRooms)
	{
		CompleteDepth();
	}
}

void AFPSRLGameState::RecountRooms()
{
	if (DepthLayout->HasLayout())
	{
		RequiredRooms = DepthLayout->GetRequiredEncounterCount();
		CompletedRooms = DepthLayout->GetClearedEncounterCount();
		ForceNetUpdate();
		OnDepthProgressChanged.Broadcast();
		return;
	}
	RequiredRoomList.RemoveAll([](const TWeakObjectPtr<AFPSRLRoom>& Room) { return !Room.IsValid(); });
	RequiredRooms = RequiredRoomList.Num();
	CompletedRooms = 0;
	for (const TWeakObjectPtr<AFPSRLRoom>& Room : RequiredRoomList)
	{
		CompletedRooms += Room->IsRoomComplete() ? 1 : 0;
	}
	ForceNetUpdate();
	OnDepthProgressChanged.Broadcast();
}

void AFPSRLGameState::CompleteDepth()
{
	if (bDepthComplete)
	{
		return;
	}
	bDepthComplete = true;
	DepthState = EFPSRLDepthState::Completing;
	GetWorldTimerManager().SetTimer(CompletedTimer, [this]() { DepthState = EFPSRLDepthState::Completed; ForceNetUpdate(); },
		FMath::Max(0.01f, UFPSRLRunSettings::Get().PortalActivationDelay), false);
	ForceNetUpdate();
	UE_LOG(LogFPSRL, Log, TEXT("[Depth] Area %d Depth %d complete"), AreaNumber, DepthNumber);
	OnDepthCompleted.Broadcast();
}

void AFPSRLGameState::OnRep_DepthProgress()
{
	OnDepthProgressChanged.Broadcast();
}

void AFPSRLGameState::OnRep_DepthComplete()
{
	if (bDepthComplete)
	{
		OnDepthCompleted.Broadcast();
	}
}

void AFPSRLGameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);
	if (HasAuthority())
	{
		OnPlayerLeft.Broadcast();
	}
}

void AFPSRLGameState::NotifyEncounterStarted(AFPSRLRoom* Room)
{
	if (HasAuthority())
	{
		DepthLayout->NotifyEncounterStarted(Room);
		if (Room && Room->RoomType == ERoomType::Boss && FinalLevelBossState != EFPSRLBossState::Defeated)
		{
			FinalLevelBossState = EFPSRLBossState::Active;
			ForceNetUpdate();
		}
	}
}

void AFPSRLGameState::RefreshFinalLevelBossState()
{
	// The boss arena's streaming state -> NotStarted / Loading; the fight itself sets Active and Defeated.
	if (!HasAuthority() || FinalLevelBossState == EFPSRLBossState::Active || FinalLevelBossState == EFPSRLBossState::Defeated)
	{
		return;
	}
	for (int32 Index = 0; Index < DepthLayout->Placements.Num(); ++Index)
	{
		const UFPSRLRoomDefinition* Room = DepthLayout->Placements[Index].Room;
		if (Room && Room->RoomType == ERoomType::Boss && DepthLayout->RoomStates.IsValidIndex(Index))
		{
			const EFPSRLBossState NewState = DepthLayout->RoomStates[Index] == EFPSRLRoomState::Loading ? EFPSRLBossState::Loading : EFPSRLBossState::NotStarted;
			if (NewState != FinalLevelBossState)
			{
				FinalLevelBossState = NewState;
				ForceNetUpdate();
			}
			return;
		}
	}
}

void AFPSRLGameState::OnRep_LevelComplete()
{
	AFPSRLPlayerController* PC = bLevelComplete ? Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
	if (PC && PC->IsLocalController())
	{
		PC->ShowNotice(NSLOCTEXT("FPSRL", "LevelComplete", "Level Complete"));
	}
}
