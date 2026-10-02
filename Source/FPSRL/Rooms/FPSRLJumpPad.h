// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLJumpPad.generated.h"

class UBoxComponent;
class UNavModifierComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class ACharacter;

/**
 * A jump pad for arenas: launches a player who steps on it with Velocity (world space; rooms are chained without
 * rotation). Players only: enemies are never launched (they would land off their navigation, e.g. in a hazard), and
 * their navigation avoids the pad's footprint. A cyan disc with an arrow along the launch. No Tick.
 */
UCLASS()
class FPSRL_API AFPSRLJumpPad : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLJumpPad();

	/** Launch velocity (cm/s, world space). Gravity 980: apex = Z^2 / 1960. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump Pad")
	FVector Velocity = FVector(0.f, 0.f, 800.f);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump Pad")
	TObjectPtr<UBoxComponent> Trigger;

	/** Bright cyan disc on the floor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump Pad")
	TObjectPtr<UStaticMeshComponent> Disc;

	/** Cyan arrow along the launch direction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump Pad")
	TObjectPtr<UStaticMeshComponent> Arrow;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	void PointArrow();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jump Pad")
	TObjectPtr<UNavModifierComponent> NavModifier;

private:
	UFUNCTION()
	void OnLaunchedLanded(const FHitResult& Hit);

	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
