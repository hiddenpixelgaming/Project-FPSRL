// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLRoomConnector.h"
#include "Components/ArrowComponent.h"

AFPSRLRoomConnector::AFPSRLRoomConnector()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->ArrowColor = FColor(0, 200, 255);
	Arrow->ArrowSize = 3.f;
	Arrow->bIsScreenSizeScaled = false;
	SetRootComponent(Arrow);
}
