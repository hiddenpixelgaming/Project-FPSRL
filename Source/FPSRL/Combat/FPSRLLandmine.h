// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLLandmine.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * A Ground Juggernaut landmine: persistent area denial on the arena floor. Placed by the boss (DeployMines attack) it
 * shows its trigger area at once (a see-through red disc) with a dark mine in the middle while it arms; armed, the mine
 * glows red. A player stepping into the trigger area sets it off once: an AoE blast (AFPSRLGroundStrike, no delay) for
 * its damage around it, then it is gone. It never moves. Stays until triggered or its boss is gone (the boss clears its
 * mines when the encounter ends).
 *
 * Server-authoritative: only the server places, arms and detonates; every machine sees the same mines (replicated).
 * No Tick: arming is a timer, triggering is an overlap.
 */
UCLASS(NotBlueprintable)
class FPSRL_API AFPSRLLandmine : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLLandmine();

	/** Server: a mine at GroundLocation (the floor). Source = the boss that placed it. */
	static AFPSRLLandmine* Spawn(APawn* Source, const FVector& GroundLocation, float TriggerRadius, float ExplosionRadius, float Damage, float ArmSeconds);

	/** The mines a boss has down now. */
	static TArray<AFPSRLLandmine*> GetMinesOf(const APawn* Source);

	bool IsArmed() const { return bArmed; }
	float GetTriggerRadius() const { return TriggerRadius; }

	/** Server: blow it up now (a player stepped in, or a test). */
	void Detonate();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_Armed();

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void Arm();
	bool IsTriggeringPlayer(const AActor* Actor) const;

	UPROPERTY(Replicated)
	float TriggerRadius = 150.f;

	UPROPERTY(ReplicatedUsing = OnRep_Armed)
	bool bArmed = false;

	float ExplosionRadius = 300.f;
	float Damage = 40.f;
	float ArmSeconds = 1.5f;
	bool bDetonated = false;
	FTimerHandle ArmTimer;

	UPROPERTY(VisibleAnywhere, Category = "Mine")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Area;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Body;
};
