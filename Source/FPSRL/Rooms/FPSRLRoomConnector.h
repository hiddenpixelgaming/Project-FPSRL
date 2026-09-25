// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLRoomConnector.generated.h"

class UArrowComponent;

/**
 * Marks where the next room attaches: place one in a room level at its exit doorway, arrow pointing out of the room
 * (away from it). The next room's level origin is snapped onto this transform, so author every room level with its
 * entrance at the origin, facing +X (into the room).
 *
 * The Depth's entry map (the Depth definition's Map) has one too: generated rooms start there.
 * Invisible in game; no collision, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLRoomConnector : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLRoomConnector();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Connector")
	TObjectPtr<UArrowComponent> Arrow;
};
