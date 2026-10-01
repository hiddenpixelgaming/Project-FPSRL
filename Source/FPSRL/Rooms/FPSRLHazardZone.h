// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLHazardZone.generated.h"

class UBoxComponent;
class UNavModifierComponent;

/**
 * An environmental hazard area (a burning pool, toxic floor...): it hurts only while a character stands in it, so it
 * shapes where players choose to fight instead of punishing them for existing. Server: a timer (no Tick) damages the
 * pawns inside every DamageInterval. Enemies path around it (its footprint is excluded from navigation) but still take
 * the damage if pushed in (melee knockback). The visible surface is level geometry; this is the logic box.
 */
UCLASS()
class FPSRL_API AFPSRLHazardZone : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLHazardZone();

	/** Damage per second to anyone standing in it (project balancing parameter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard", meta = (ClampMin = "0"))
	float DamagePerSecond = 10.f;

	/** Seconds between damage ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard", meta = (ClampMin = "0.1"))
	float DamageInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	bool bAffectsPlayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	bool bAffectsEnemies = true;

	/** Enemies' navigation avoids it (their paths and positions stay out of it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hazard")
	bool bBlocksEnemyNavigation = true;

	UBoxComponent* GetZone() const { return Zone; }

	/** Damage ticks applied so far (tests). */
	int32 GetTicksApplied() const { return TicksApplied; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The hazard volume: a pawn whose capsule reaches into it is standing in the hazard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard")
	TObjectPtr<UBoxComponent> Zone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hazard")
	TObjectPtr<UNavModifierComponent> NavModifier;

private:
	void ApplyDamage();

	FTimerHandle DamageTimer;
	int32 TicksApplied = 0;
};
