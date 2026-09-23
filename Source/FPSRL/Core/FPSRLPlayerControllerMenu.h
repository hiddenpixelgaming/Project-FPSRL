// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRLPlayerControllerMenu.generated.h"

/**
 * PlayerController for the main Menu map. BP_PlayerControllerMenu derives from this (and creates WB_Menu).
 *
 * Puts input into menu mode: visible cursor, UI-only input, mouse not locked to the window (so Alt+Tab and
 * moving to another monitor work). Without this the Menu inherits the default Game-only mode, which hides and
 * captures the mouse. Restores game input on the way out so the next level's mouse-look isn't left in UI mode.
 * Local-only: input mode is a per-machine concern, nothing here replicates.
 */
UCLASS()
class FPSRL_API AFPSRLPlayerControllerMenu : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRLPlayerControllerMenu();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
