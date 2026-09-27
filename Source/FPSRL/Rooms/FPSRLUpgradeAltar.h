// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLBoonTerminal.h"
#include "FPSRLUpgradeAltar.generated.h"

/**
 * Upgrade Altar: strengthens something the player already owns, never gives anything new. Each player may use it
 * once: the server offers up to three of their upgradeable Aspects and Blessings (any channel) and applies the one
 * they pick (UFPSRLBoonComponent::BeginUpgradeSelection). An upgrade never changes a channel's Blessing count.
 *
 * Same rules as the Blessing altar it derives from: optional, personal, locked until its Room is cleared (or open
 * from the start without a Room), and the exit never waits for it. Placeable as is: a gold pillar by default.
 */
UCLASS()
class FPSRL_API AFPSRLUpgradeAltar : public AFPSRLBoonTerminal
{
	GENERATED_BODY()

public:
	AFPSRLUpgradeAltar();

protected:
	virtual void BeginPlay() override;
	virtual bool BeginPlayerSelection(AFPSRLPlayerState* Player) override;

	/** Tint of the default pillar (the engine's basic shape material). */
	UPROPERTY(EditAnywhere, Category = "Terminal")
	FLinearColor PillarColor = FLinearColor(1.f, 0.72f, 0.15f);
};
