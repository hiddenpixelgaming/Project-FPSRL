// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "FPSRLPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AFPSRLBoonTerminal;
class AFPSRLExitPortal;
class UFPSRLPortalMenuWidget;
class UFPSRLPortalStatusWidget;
class UFPSRLPauseMenuWidget;
class UFPSRLSelectionWidget;

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
 * Aspect / Boon selection (requests only; the server validates everything in the player's components):
 *  - ServerSelectAspect (Lobby, after the weapon), ServerSelectBoon, ServerRerollBoons. Each request carries the
 *    selection event id it was made for, so stale or duplicate requests are rejected.
 *  - The owning client shows a selection screen for pending choices (default C++ layout, restylable with a
 *    Blueprint subclass via AspectSelectionClass / BoonSelectionClass). The Aspect screen opens by itself once
 *    the weapon is picked; the Boon screen opens when the player uses a Boon altar (UseBoonAltar -> the server rolls
 *    their options) and can be closed and reopened until the choice resolves. No timer: altars are optional.
 *  - Run: the host's Start begins the run set in Project Settings > FPSRL Run (UFPSRLRunSubsystem), or travels to
 *    MatchMap when none is set.
 *  - Run state: arriving in a Depth applies the aspect and re-grants carried boons (BeginRunState); returning to the
 *    Lobby clears only temporary Boon/Aspect state (ClearRunState).
 *  - Currency: the local profile's TalentEssence (Soul Fragments) is reported to the server once per PlayerState and
 *    written back to the save file whenever the server changes it.
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

	/** True when there is at least one player and every player is ready with a complete loadout (weapon + aspect).
	 *  Exec node to match the old BP function. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Lobby")
	bool AreAllPlayersReady() const;

	// --- Aspect / Boon selection -------------------------------------------------------------------------------

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Aspect")
	void ServerSelectAspect(int32 EventId, int32 OptionIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerSelectBoon(int32 EventId, int32 OptionIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerRerollBoons(int32 EventId);

	/** Client: the owning player's saved Soul Fragments balance (sent once per PlayerState). */
	UFUNCTION(Server, Reliable)
	void ServerReportTalentEssence(int32 Amount);

	/** Server -> owning client: persist the new balance to the local save file. */
	UFUNCTION(Client, Reliable)
	void ClientPersistentCurrencyChanged(int32 NewAmount);

	/** Local: show this player's pending Boon choice (reopens it after Close). False if nothing is pending. */
	UFUNCTION(BlueprintCallable, Category = "Boons")
	bool OpenBoonSelection();

	/** Local: this player has a Boon choice waiting to be made. */
	UFUNCTION(BlueprintPure, Category = "Boons")
	bool HasPendingBoonSelection() const;

	/** Local: the player used a Boon altar. Reopens an open choice, or asks the server for this altar's choice. */
	void UseBoonAltar(AFPSRLBoonTerminal* Altar);

	UFUNCTION(Server, Reliable)
	void ServerUseBoonAltar(AFPSRLBoonTerminal* Altar);

	/** Server -> owning client: the altar refused (locked, out of range, already used, nothing to offer). */
	UFUNCTION(Client, Reliable)
	void ClientBoonAltarRejected();

	// --- Exit portal ---------------------------------------------------------------------------------------------

	/** Local: the player interacted with an active exit portal. Shows the Continue / Cancel menu. */
	void OpenPortalMenu(AFPSRLExitPortal* Portal);

	/** Local: the portal's votes or state changed. Updates the open menu and the HUD tracker. */
	void RefreshPortalUI(AFPSRLExitPortal* Portal);

	UFUNCTION(Server, Reliable)
	void ServerPortalVote(AFPSRLExitPortal* Portal, bool bContinue);

	/** Dev cheats (host). Console: FPSRLGiveBoon /Game/.../DA_Boon_X.DA_Boon_X  |  FPSRLOfferBoon (a choice without an altar) */
	UFUNCTION(Exec)
	void FPSRLGiveBoon(const FString& BoonAssetPath);

	UFUNCTION(Exec)
	void FPSRLOfferBoon();

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
	virtual void OnRep_PlayerState() override;

	/** Selection screens (default C++ layout; set a Blueprint subclass to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Boons")
	TSubclassOf<UFPSRLSelectionWidget> AspectSelectionClass;

	UPROPERTY(EditDefaultsOnly, Category = "Boons")
	TSubclassOf<UFPSRLSelectionWidget> BoonSelectionClass;

	/** Exit portal dialog and HUD vote tracker (default C++ layouts; set Blueprint subclasses to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Portal")
	TSubclassOf<UFPSRLPortalMenuWidget> PortalMenuClass;

	UPROPERTY(EditDefaultsOnly, Category = "Portal")
	TSubclassOf<UFPSRLPortalStatusWidget> PortalStatusClass;

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

	bool IsInLobby() const;

	// Local selection UI
	void BindToPlayerStateComponents();
	UFUNCTION() void RefreshAspectSelectionUI();
	UFUNCTION() void RefreshBoonSelectionUI();
	void ShowSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget, TSubclassOf<UFPSRLSelectionWidget> WidgetClass);
	void HideSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget);
	void HandleAspectChoice(int32 OptionIndex);
	void HandleBoonChoice(int32 OptionIndex);
	void HandleBoonReroll();
	void HandleBoonClose();

	void ClosePortalMenu();
	void HandlePortalChoice(bool bContinue);

	/** Some modal screen (aspect, boon, portal) is up: keep UI input and the cursor. */
	bool IsAnyModalOpen() const;

	/** Local: the player opened the Boon screen at a terminal (cleared when they close it or the choice resolves). */
	bool bBoonSelectionOpen = false;

	// Local profile bridge (BP_GameInstanceBase.CurrentPlayerProfile.TalentEssence) until the save game moves to C++.
	bool ReadLocalTalentEssence(int32& OutAmount) const;
	void WriteLocalTalentEssence(int32 Amount) const;
	void ReportTalentEssenceIfNeeded();

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLSelectionWidget> AspectSelectionWidget;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLSelectionWidget> BoonSelectionWidget;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPortalMenuWidget> PortalMenu;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPortalStatusWidget> PortalStatus;

	/** Portal the open menu belongs to. */
	TWeakObjectPtr<AFPSRLExitPortal> MenuPortal;

	TWeakObjectPtr<APlayerState> BoundPlayerState;
	TWeakObjectPtr<APlayerState> ReportedPlayerState;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPauseMenuWidget> PauseMenu;

	/** Input state before the menu opened, restored on close. */
	bool bCursorWasVisible = false;

	FDelegateHandle DestroySessionHandle;
};
