// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FPSRLEnemyBodySubsystem.generated.h"

class APawn;

/**
 * Dresses enemies in their role's character (UFPSRLEnemyDefinition::BodyMesh, e.g. a Sci-Fi Trooper) on every machine.
 *
 * The enemy class keeps its own skeletal mesh, animation blueprint, weapon and collision; that mesh is hidden (still
 * animating) and the role's body follows its pose (leader pose: the bone names match the UE Mannequin layout). So the
 * enemy looks like its role without new blueprints or animation graphs, and nothing about combat changes. A missing
 * body (the licensed art isn't in Git) leaves the class's own mesh visible.
 *
 * Applied when an enemy spawns (server and clients: replicated enemies spawn on clients too), not polled.
 */
UCLASS()
class FPSRL_API UFPSRLEnemyBodySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Puts the role body on this enemy (once; nothing when its definition has none). */
	static void ApplyBody(APawn* Pawn);

private:
	void HandleActorSpawned(AActor* Actor);

	FDelegateHandle SpawnHandle;
};
