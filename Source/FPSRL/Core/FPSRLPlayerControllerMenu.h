// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRLPlayerControllerMenu.generated.h"

class UFPSRLServerBrowserWidget;

/**
 * PlayerController for the main Menu map. BP_PlayerControllerMenu derives from this (and creates WB_Menu).
 *
 * Puts input into menu mode: visible cursor, UI-only input, mouse not locked to the window (so Alt+Tab and
 * moving to another monitor work). Without this the Menu inherits the default Game-only mode, which hides and
 * captures the mouse. Restores game input on the way out so the next level's mouse-look isn't left in UI mode.
 * Local-only: input mode is a per-machine concern, nothing here replicates.
 *
 * Also owns the Join flow: WB_Menu's JoinButton opens the server browser (UFPSRLServerBrowserWidget) instead of
 * joining the first game found, and a join the host refused lands the player back in the browser with the reason.
 */
UCLASS()
class FPSRL_API AFPSRLPlayerControllerMenu : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRLPlayerControllerMenu();

	/** Show the list of hosted games (Join screen). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void OpenServerBrowser();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Restyle hook: a Widget Blueprint deriving from UFPSRLServerBrowserWidget; the C++ layout is used if unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	TSubclassOf<UFPSRLServerBrowserWidget> ServerBrowserClass;

private:
	/** Point the Menu widget's "JoinButton" at OpenServerBrowser (its Blueprint handler joined the first result). */
	void TakeOverJoinButton();

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLServerBrowserWidget> ServerBrowser;
};
