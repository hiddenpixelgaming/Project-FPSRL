// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLFallVolume.generated.h"

class UBoxComponent;

/**
 * Catches anything that falls out of a Depth: a wide box under every room of the layout (spawned by
 * UFPSRLDepthLayoutComponent, sized from the layout plus FPSRLRunSettings.FallVolumeMargin / FallVolumeDepth).
 * Server only. Players get the FallResponse from the run settings: back on the floor of the room they fell from minus
 * FallDamageFraction of max health (the real game), or killed (playtesting). Enemies that fall are killed so their
 * room can still be cleared; anything else that falls in is destroyed. Event-driven (overlap), no Tick.
 */
UCLASS(NotPlaceable)
class FPSRL_API AFPSRLFallVolume : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLFallVolume();

	/** Cover this world-space box. */
	void Cover(const FBox& Box);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Fall")
	TObjectPtr<UBoxComponent> Volume;

private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void CatchPlayer(APawn* Pawn);
};
