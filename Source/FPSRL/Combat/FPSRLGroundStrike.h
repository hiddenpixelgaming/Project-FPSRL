// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLGroundStrike.generated.h"

class UStaticMeshComponent;

/**
 * A marked spot on the ground that explodes: a see-through red disc the size of the blast, filling up over the warning
 * time, then a flash and AoE damage to every player in the radius (once, through the normal damage path; players well
 * above or below it - on a platform - are out of it). Damage 0 = a warning only (the Juggernaut's shockwave charge).
 * Delay 0 = it goes off at once (a mine's blast).
 *
 * Used by the Ground Juggernaut's missiles (UFPSRLEnemyBehaviorProfile GroundStrike), its landmines and its charge.
 * Spawned by the server; replicated (every machine draws the same mark and flash on its own clock). Ticks only while it
 * lives (the warning plus a moment).
 */
UCLASS(NotBlueprintable)
class FPSRL_API AFPSRLGroundStrike : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLGroundStrike();

	/** Server: a strike at GroundLocation (the floor) going off after Delay seconds. Source = the enemy whose attack it is. */
	static AFPSRLGroundStrike* Spawn(APawn* Source, const FVector& GroundLocation, float Radius, float Delay, float Damage);

	/** Players it hit (tests). */
	int32 GetPlayersHit() const { return PlayersHit; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	void Impact();
	UStaticMeshComponent* MakeDisc(const TCHAR* Name, const TCHAR* MaterialPath);

	UPROPERTY(Replicated)
	float Radius = 300.f;

	UPROPERTY(Replicated)
	float Delay = 1.f;

	UPROPERTY(Replicated)
	float Damage = 0.f;

	double StartTime = 0.0;
	bool bImpacted = false;
	int32 PlayersHit = 0;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Area;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Fill;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Flash;
};
