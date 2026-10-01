// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLBoonTerminal.h"
#include "FPSRLHealingAltar.generated.h"

/**
 * Healing Altar: restores the player to full health. One of the rewards a combat room can leave by its exit (with the
 * Blessing and Upgrade Altars, see UFPSRLDepthDefinition::TraversalRewards).
 *
 * Same rules as the Blessing altar it derives from: personal (each player may use it once), locked until its Room is
 * cleared (or open from the start without a Room), optional. A player already at full health keeps their use for later.
 * Placeable as is: a green pillar with a white health cross above it.
 */
UCLASS()
class FPSRL_API AFPSRLHealingAltar : public AFPSRLBoonTerminal
{
	GENERATED_BODY()

public:
	AFPSRLHealingAltar();

protected:
	virtual bool BeginPlayerSelection(AFPSRLPlayerState* Player) override;
	virtual FText GetNothingToOfferText() const override;

public:
	virtual bool OpensSelectionScreen() const override { return false; }

public:
	/** Always a Healing Altar: never takes the Upgrade look. */
	virtual void RefreshLocalAppearance() override {}
};
