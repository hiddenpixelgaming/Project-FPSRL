// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLRewardSpawnPoint.h"
#include "Components/ArrowComponent.h"

AFPSRLRewardSpawnPoint::AFPSRLRewardSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowColor = FColor(255, 200, 40);
	Arrow->SetHiddenInGame(true);
}
