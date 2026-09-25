// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLEnemySpawnPoint.generated.h"

class APawn;
class UArrowComponent;
class UCapsuleComponent;

/**
 * Where a room spawns one enemy when its encounter starts (AFPSRLRoom). Nothing exists here before the first player
 * trips the room's combat trigger, so enemies can't see, shoot or wander into other rooms early.
 * The arrow shows the direction the enemy faces. Editor-only visuals; no collision, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLEnemySpawnPoint();

	/** Enemy to spawn. Empty = the room's EnemyClass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TSubclassOf<APawn> EnemyClass;

	/** Server: spawn the enemy (with its AI controller). Null if the class is missing or the spawn failed. */
	APawn* SpawnEnemy(TSubclassOf<APawn> FallbackClass) const;

protected:
	/** Enemy-sized outline, so placement is easy to judge in the editor. */
	UPROPERTY(VisibleAnywhere, Category = "Spawn")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "Spawn")
	TObjectPtr<UArrowComponent> Arrow;
};
