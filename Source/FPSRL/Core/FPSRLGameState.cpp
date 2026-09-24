// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLGameState.h"
#include "Core/FPSRLRunSubsystem.h"
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
}

void AFPSRLGameState::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (const UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		AreaNumber = RunSubsystem->GetAreaNumber();
		DepthNumber = RunSubsystem->GetDepthNumber();
	}

	// Rooms register in their own BeginPlay (same frame). Check on the next tick so a Depth without required rooms
	// (merchant / preparation) completes immediately, and one with rooms waits for them.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Depth] Area %d Depth %d: %d required room(s)"), AreaNumber, DepthNumber, RequiredRooms);
		if (RequiredRooms == 0)
		{
			CompleteDepth();
		}
	}));
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
	RecountRooms();
	if (RequiredRooms > 0 && CompletedRooms >= RequiredRooms)
	{
		CompleteDepth();
	}
}

void AFPSRLGameState::RecountRooms()
{
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
