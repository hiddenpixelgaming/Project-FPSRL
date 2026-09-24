// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLRoom.h"
#include "Components/BoxComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLGameState.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLDoor.h"
#include "Rooms/FPSRLTriggerVolume.h"
#include "FPSRL.h"

AFPSRLRoom::AFPSRLRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;	// small state, and clients need it for doors/altars/HUD anywhere in the Depth

	RoomBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RoomBounds"));
	RoomBounds->InitBoxExtent(FVector(1500.f, 1500.f, 500.f));
	RoomBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoomBounds->SetGenerateOverlapEvents(false);
	RoomBounds->ShapeColor = FColor(255, 140, 0);
	SetRootComponent(RoomBounds);
}

void AFPSRLRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLRoom, bCombatStarted);
	DOREPLIFETIME(AFPSRLRoom, bRoomComplete);
	DOREPLIFETIME(AFPSRLRoom, RemainingEnemies);
}

void AFPSRLRoom::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (bRequiredForDepth)
	{
		if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
		{
			GameState->RegisterRequiredRoom(this);
		}
	}

	if (CombatTrigger)
	{
		CombatTrigger->OnTriggered.AddUniqueDynamic(this, &ThisClass::HandleCombatTriggered);
	}
	else
	{
		StartCombat();
	}
}

void AFPSRLRoom::HandleCombatTriggered(AActor* TriggeringActor)
{
	UE_LOG(LogFPSRL, Log, TEXT("[Room %s] combat triggered by %s"), *GetActorNameOrLabel(), *GetNameSafe(TriggeringActor));
	StartCombat();
}

TArray<AActor*> AFPSRLRoom::GatherEnemies() const
{
	TArray<AActor*> Result;
	if (!Enemies.IsEmpty())
	{
		for (AActor* Enemy : Enemies)
		{
			if (Enemy)
			{
				Result.AddUnique(Enemy);
			}
		}
		return Result;
	}

	if (!EnemyClass)
	{
		return Result;
	}
	const FTransform BoundsTransform = RoomBounds->GetComponentTransform();
	const FBox LocalBox(-RoomBounds->GetUnscaledBoxExtent(), RoomBounds->GetUnscaledBoxExtent());
	for (TActorIterator<AActor> It(GetWorld(), EnemyClass); It; ++It)
	{
		if (LocalBox.IsInside(BoundsTransform.InverseTransformPosition(It->GetActorLocation())))
		{
			Result.Add(*It);
		}
	}
	return Result;
}

void AFPSRLRoom::StartCombat()
{
	if (!HasAuthority() || bCombatStarted)
	{
		return;
	}
	bCombatStarted = true;

	if (ExitDoor)
	{
		ExitDoor->Lock();
	}

	// Track only enemies that are alive right now; each health component's OnDeath fires once.
	RemainingEnemies = 0;
	for (AActor* Enemy : GatherEnemies())
	{
		UFPSRLHealthComponent* Health = Enemy->FindComponentByClass<UFPSRLHealthComponent>();
		if (Health && !Health->IsDead())
		{
			Health->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleEnemyDeath);
			++RemainingEnemies;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Room %s] combat started: %d living enemies"), *GetActorNameOrLabel(), RemainingEnemies);

	ForceNetUpdate();
	OnRep_RoomState();

	if (RemainingEnemies == 0)
	{
		CompleteRoom();
	}
}

void AFPSRLRoom::HandleEnemyDeath(AController* Killer, AActor* Causer)
{
	if (!HasAuthority() || bRoomComplete)
	{
		return;
	}
	RemainingEnemies = FMath::Max(0, RemainingEnemies - 1);
	ForceNetUpdate();
	if (RemainingEnemies == 0)
	{
		CompleteRoom();
	}
}

void AFPSRLRoom::CompleteRoom()
{
	if (bRoomComplete)
	{
		return;
	}
	bRoomComplete = true;
	UE_LOG(LogFPSRL, Log, TEXT("[Room %s] cleared"), *GetActorNameOrLabel());

	if (ExitDoor)
	{
		ExitDoor->Unlock();
	}
	ForceNetUpdate();
	OnRep_RoomState();

	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->NotifyRoomCompleted(this);
	}
}

void AFPSRLRoom::OnRep_RoomState()
{
	// Runs on the server by hand and on clients via replication; each event fires once per machine.
	if (bCombatStarted && !bBroadcastCombatStarted)
	{
		bBroadcastCombatStarted = true;
		OnCombatStarted.Broadcast();
	}
	if (bRoomComplete && !bBroadcastCompleted)
	{
		bBroadcastCompleted = true;
		OnRoomCompleted.Broadcast();
	}
}
