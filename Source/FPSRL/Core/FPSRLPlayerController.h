// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRLPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UFPSRLPauseMenuWidget;

/**
 * Base PlayerController for gameplay levels (Lobby and Arena). BP_PlayerControllerLobby and
 * BP_FirstPersonPlayerController (and so BP_ShooterPlayerController) derive from this.
 *
 * Currently owns the pause menu (Step C will move more here: lobby RPCs, weapon selection).
 *  - PauseAction (IA_Pause: Esc / gamepad Start) opens the menu via PauseMappingContext (IMC_Pause).
 *  - While open: UI-only input, visible cursor, mouse not locked (Alt+Tab works); gameplay input is blocked.
 *  - Closing restores whatever mode was active before (game-only, or the Lobby's game+UI with cursor).
 * Everything here is local to the owning machine; nothing replicates and the world is never paused.
 */
UCLASS()
class FPSRL_API AFPSRLPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void OpenPauseMenu();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void ClosePauseMenu();

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void TogglePauseMenu();

	UFUNCTION(BlueprintPure, Category = "Pause")
	bool IsPauseMenuOpen() const;

	/** Leave the current game: host ends the session for everyone, a client disconnects. Returns to the Menu map. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void QuitToMainMenu();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TObjectPtr<UInputMappingContext> PauseMappingContext;

	/** Widget to show. Defaults to the C++ class, which builds its own layout; set a Blueprint subclass to restyle. */
	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TSubclassOf<UFPSRLPauseMenuWidget> PauseMenuClass;

private:
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPauseMenuWidget> PauseMenu;

	/** Input state before the menu opened, restored on close. */
	bool bCursorWasVisible = false;

	FDelegateHandle DestroySessionHandle;
};
