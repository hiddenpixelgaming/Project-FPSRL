// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLEnemySpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "FPSRL.h"

AFPSRLEnemySpawnPoint::AFPSRLEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(40.f, 90.f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Capsule->SetGenerateOverlapEvents(false);
	Capsule->SetHiddenInGame(true);
	Capsule->ShapeColor = FColor(255, 60, 60);
	SetRootComponent(Capsule);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Capsule);
	Arrow->ArrowColor = FColor(255, 60, 60);
}

APawn* AFPSRLEnemySpawnPoint::SpawnEnemy(TSubclassOf<APawn> FallbackClass) const
{
	const TSubclassOf<APawn> Class = EnemyClass ? EnemyClass : FallbackClass;
	UWorld* World = GetWorld();
	if (!Class || !World || !HasAuthority())
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Spawn %s] no enemy class"), *GetActorNameOrLabel());
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Enemy = World->SpawnActor<APawn>(Class, GetActorLocation(), GetActorRotation(), Params);
	if (Enemy && !Enemy->GetController())
	{
		Enemy->SpawnDefaultController();	// its AI (StateTree) runs from here
	}
	return Enemy;
}
