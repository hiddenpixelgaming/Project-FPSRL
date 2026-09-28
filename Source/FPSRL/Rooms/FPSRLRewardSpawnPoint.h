// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLRewardSpawnPoint.generated.h"

class UArrowComponent;
class UBillboardComponent;

/**
 * Where a Traversal space's reward appears. Place one in each traversal level; when the traversal loads, the server
 * spawns whatever the Depth's data rolled for it (Blessing Altar, Upgrade Altar, a future interactable, or nothing)
 * here, using Project Settings > FPSRL Run > Traversal Reward Classes. Marker only: nothing in game.
 */
UCLASS()
class FPSRL_API AFPSRLRewardSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLRewardSpawnPoint();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Reward")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Reward")
	TObjectPtr<UArrowComponent> Arrow;
};
