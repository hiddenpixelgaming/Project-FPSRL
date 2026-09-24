// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLTriggerVolume.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFPSRLTriggeredEvent, AActor*, TriggeringActor);

/**
 * Server-side player trigger (combat start, exit sensor, ...). BP_TriggerBox_Base derives from this.
 *
 * The overlap box is built into the class, so a placed instance needs no hand-added volume: size it with
 * TriggerBox's Box Extent in the Details panel. Only player-controlled characters trigger it; AI, projectiles
 * and physics objects are ignored.
 *
 * Server-only: overlaps are evaluated on the server, which is the one that should start combat or lock doors.
 * bHasFired replicates so clients can see the state, but OnTriggered only ever broadcasts on the server.
 * Event-driven (no Tick).
 */
UCLASS()
class FPSRL_API AFPSRLTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLTriggerVolume();

	/** Fired on the server when a player enters (once, if bFireOnce). Same signature as the old BP dispatcher. */
	UPROPERTY(BlueprintAssignable, Category = "Trigger")
	FFPSRLTriggeredEvent OnTriggered;

	/** Fire only for the first player who enters, then stay idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	bool bFireOnce = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Trigger")
	bool bHasFired = false;

	/** Server-only. Lets a fire-once trigger fire again (e.g. a reusable room). */
	UFUNCTION(BlueprintCallable, Category = "Trigger")
	void ResetTrigger();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;
};
