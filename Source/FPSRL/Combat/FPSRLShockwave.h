// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLShockwave.generated.h"

class UStaticMeshComponent;

/**
 * The Brute's landing shockwave: a low red ring that spreads along the ground from where it landed. A player it passes
 * takes its damage once - unless their feet are above the ring (they jumped over it) or they stand on other ground well
 * above or below it. Spawned by the server; replicated so every machine draws the ring growing at the same speed.
 * Ticks only while it lives (about a second).
 */
UCLASS(NotBlueprintable)
class FPSRL_API AFPSRLShockwave : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLShockwave();

	/** Server: a ring from GroundLocation (the floor under the source) dealing Damage, reaching MaxRadius at Speed, Height
	 *  tall (what a player has to clear). Source = the enemy whose attack it is. */
	static AFPSRLShockwave* Spawn(APawn* Source, const FVector& GroundLocation, float Damage, float MaxRadius, float Speed, float Height);

	float GetRadius() const;
	int32 GetPlayersHit() const { return Struck.Num(); }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateRing();
	void DamagePlayers(float FromRadius, float ToRadius);

	UPROPERTY(Replicated)
	float MaxRadius = 900.f;

	UPROPERTY(Replicated)
	float Speed = 1000.f;

	UPROPERTY(Replicated)
	float Height = 45.f;

	float Damage = 0.f;
	double StartTime = 0.0;
	float LastRadius = 0.f;

	TArray<TWeakObjectPtr<AActor>> Struck;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;
};
