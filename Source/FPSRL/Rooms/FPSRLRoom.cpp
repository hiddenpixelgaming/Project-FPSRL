// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLRoom.h"
#include "Components/BoxComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLGameState.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "Rooms/FPSRLDoor.h"
#include "Rooms/FPSRLEnemySpawnPoint.h"
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

bool AFPSRLRoom::IsInsideBounds(const FVector& WorldLocation) const
{
	const FBox LocalBox(-RoomBounds->GetUnscaledBoxExtent(), RoomBounds->GetUnscaledBoxExtent());
	return LocalBox.IsInside(RoomBounds->GetComponentTransform().InverseTransformPosition(WorldLocation));
}

TArray<AFPSRLEnemySpawnPoint*> AFPSRLRoom::GatherSpawnPoints() const
{
	TArray<AFPSRLEnemySpawnPoint*> Result;
	for (AFPSRLEnemySpawnPoint* Point : SpawnPoints)
	{
		if (IsValid(Point))
		{
			Result.AddUnique(Point);
		}
	}
	if (SpawnPoints.IsEmpty())
	{
		for (TActorIterator<AFPSRLEnemySpawnPoint> It(GetWorld()); It; ++It)
		{
			if (IsInsideBounds(It->GetActorLocation()))
			{
				Result.Add(*It);
			}
		}
	}
	return Result;
}

TArray<AActor*> AFPSRLRoom::GatherEnemies() const
{
	// Pre-placed enemies. A destroyed body (IsValid false) is skipped; its kill already happened.
	TArray<AActor*> Result;
	if (!Enemies.IsEmpty())
	{
		for (AActor* Enemy : Enemies)
		{
			if (IsValid(Enemy))
			{
				Result.AddUnique(Enemy);
			}
		}
		return Result;
	}

	// Without spawn points, fall back to every EnemyClass actor already standing in the room.
	if (!EnemyClass || !GatherSpawnPoints().IsEmpty())
	{
		return Result;
	}
	for (TActorIterator<AActor> It(GetWorld(), EnemyClass); It; ++It)
	{
		if (IsValid(*It) && IsInsideBounds(It->GetActorLocation()))
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

	// Spawn this room's enemies now (nothing existed at the spawn points before), then track every living one.
	TArray<AActor*> ToTrack = GatherEnemies();
	const TSubclassOf<APawn> DefaultEnemy = (EnemyClass && EnemyClass->IsChildOf(APawn::StaticClass())) ? TSubclassOf<APawn>(EnemyClass.Get()) : nullptr;
	for (const AFPSRLEnemySpawnPoint* Point : GatherSpawnPoints())
	{
		if (APawn* Spawned = Point->SpawnEnemy(DefaultEnemy))
		{
			ToTrack.Add(Spawned);
		}
	}

	// Each health component's OnDeath fires once.
	RemainingEnemies = 0;
	for (AActor* Enemy : ToTrack)
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
