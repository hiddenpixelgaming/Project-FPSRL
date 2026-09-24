// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "FPSRLPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UFPSRLPauseMenuWidget;

/** One lobby-selectable weapon: its tag and the weapon actor class the character is given. */
USTRUCT(BlueprintType)
struct FFPSRLWeaponOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Categories = "Weapon"))
	FGameplayTag WeaponTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AActor> WeaponClass;
};

/**
 * Base PlayerController for gameplay levels (Lobby and Arena). BP_PlayerControllerLobby and
 * BP_FirstPersonPlayerController (and so BP_ShooterPlayerController) derive from this.
 *
 * Lobby (server RPCs; declared in C++ so they are guaranteed Server + Reliable):
 *  - ServerToggleReady / ServerRequestStartMatch / ServerSelectWeapon(Weapon.* tag). The client only asks;
 *    the server validates and writes the result into the replicated AFPSRLPlayerState.
 *  - Start is host-only and requires every player ready. It seamless-travels to MatchMap.
 *  - The selected weapon is equipped on the server whenever this controller possesses a pawn outside the lobby
 *    (i.e. on arrival in the Arena), and immediately when picked at the weapon station.
 *
 * Pause menu (local only, never pauses the world):
 *  - PauseAction (IA_Pause: Esc / gamepad Start) opens the menu via PauseMappingContext (IMC_Pause).
 *  - While open: UI-only input, visible cursor, mouse not locked (Alt+Tab works); gameplay input is blocked.
 *  - Closing restores whatever mode was active before (game-only, or the Lobby's game+UI with cursor).
 */
UCLASS()
class FPSRL_API AFPSRLPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// --- Lobby -------------------------------------------------------------------------------------------------

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerToggleReady();

	/** Host-only: travels everyone to MatchMap once all players are ready. Ignored for clients. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerRequestStartMatch();

	/** Pick a lobby weapon by tag (must be one of WeaponOptions). Equips it right away. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerSelectWeapon(FGameplayTag WeaponTag);

	/** True when there is at least one player and every player is ready. Exec node to match the old BP function. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Lobby")
	bool AreAllPlayersReady() const;

	// --- Pause -------------------------------------------------------------------------------------------------

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
	virtual void OnPossess(APawn* InPawn) override;

	/** Weapons the lobby station offers. First entry is the fallback if a tag is unknown. */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	TArray<FFPSRLWeaponOption> WeaponOptions;

	/** Map the host's Start Match travels to. */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	TSoftObjectPtr<UWorld> MatchMap;

	/** Level name treated as the lobby (no auto-equip on spawn there; players pick at the station). */
	UPROPERTY(EditDefaultsOnly, Category = "Lobby")
	FString LobbyLevelName = TEXT("Lvl_Lobby");

	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TObjectPtr<UInputMappingContext> PauseMappingContext;

	/** Widget to show. Defaults to the C++ class, which builds its own layout; set a Blueprint subclass to restyle. */
	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TSubclassOf<UFPSRLPauseMenuWidget> PauseMenuClass;

private:
	/** Server: give the pawn the weapon from this player's PlayerState. */
	void EquipSelectedWeapon(APawn* InPawn);
	const FFPSRLWeaponOption* FindWeaponOption(const FGameplayTag& WeaponTag) const;

	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPauseMenuWidget> PauseMenu;

	/** Input state before the menu opened, restored on close. */
	bool bCursorWasVisible = false;

	FDelegateHandle DestroySessionHandle;
};
