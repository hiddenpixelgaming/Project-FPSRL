// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLBoonTerminal.h"
#include "FPSRLUpgradeAltar.generated.h"

/**
 * Upgrade Altar: strengthens a Blessing the player already owns, never gives anything new (Aspects are never
 * upgraded). Each player may use it once: the server offers up to three of their owned Blessings below their maximum
 * upgrade level (any channel) and applies the one they pick (UFPSRLBoonComponent::BeginUpgradeSelection). Upgrades
 * stack across altars and never change a channel's Blessing count.
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
	virtual bool BeginPlayerSelection(AFPSRLPlayerState* Player) override;

public:
	/** Already an Upgrade Altar: always looks and reads like one. */
	virtual void RefreshLocalAppearance() override {}
};
