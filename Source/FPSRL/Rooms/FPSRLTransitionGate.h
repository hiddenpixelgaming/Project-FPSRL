// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLTransitionGate.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * The way out of a Traversal space: closed until the room after it has loaded on EVERY player's machine
 * (UFPSRLDepthLayoutComponent::ReadyThrough), so nobody walks into a room that isn't there yet for them.
 *
 * Needs no replication of its own: each machine opens it from the replicated readiness, which the server decides.
 * Place it across the traversal's exit doorway and set Door to the doorway's door: the gate then locks and opens that
 * door instead of showing its own barrier. Event-driven, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLTransitionGate : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLTransitionGate();

	UFUNCTION(BlueprintPure, Category = "Gate")
	bool IsOpen() const { return bOpen; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The doorway's door: kept locked while the gate is closed, opened with it. With a door the barrier isn't used. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gate")
	TObjectPtr<class AFPSRLDoor> Door;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate", meta = (ClampMin = "10"))
	FVector GateSize = FVector(20.f, 350.f, 300.f);

	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UBoxComponent> Blocker;

	/** Visible while closed. */
	UPROPERTY(VisibleAnywhere, Category = "Gate")
	TObjectPtr<UStaticMeshComponent> Barrier;

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void Refresh();
	void SetOpen(bool bNewOpen);

	bool bOpen = false;
	bool bStateApplied = false;	// the first SetOpen always applies (locks the door at BeginPlay)
	FDelegateHandle ReadinessHandle;
};
