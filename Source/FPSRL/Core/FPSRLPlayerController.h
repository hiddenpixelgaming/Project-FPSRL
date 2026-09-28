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
class AFPSRLReviveMarker;
class UFPSRLDeathMenuWidget;
class UFPSRLPortalMenuWidget;
class UFPSRLPortalStatusWidget;
class UFPSRLReviveWidget;
class AFPSRLProjectile;
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
 * Blessing selection (requests only; the server validates everything in the player's UFPSRLBoonComponent):
 *  - Blessing altar: ServerChooseAspect -> ServerChooseSlot (new Aspect only) -> ServerSelectBoon, with ServerAltarBack
 *    and ServerRerollBoons (Aspect step). Upgrade Altar: ServerSelectUpgrade. Each request carries
 *    the selection event id it was made for, so stale or duplicate requests are rejected.
 *  - The owning client shows one selection screen for whichever choice is pending (default C++ layout, restylable
 *    with a Blueprint subclass via BoonSelectionClass). It opens when the player uses an altar (UseBoonAltar -> the
 *    server rolls their options) and can be closed and reopened until the choice resolves. No timer: altars are optional.
 *  - Run: the host's Start begins the run set in Project Settings > FPSRL Run (UFPSRLRunSubsystem), or travels to
 *    MatchMap when none is set.
 *  - Run state: arriving in a Depth re-grants the carried Blessings and relics (BeginRunState); returning to the
 *    Lobby clears only temporary Blessing / relic state (ClearRunState).
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

	/** The lobby weapon option for a tag (null if unknown). */
	const FFPSRLWeaponOption* FindWeaponOption(const FGameplayTag& WeaponTag) const;

	/** Give a pawn a weapon through the character's BPI_WeaponHolder "Add Weapon Class" (any machine; weapons are local actors). */
	static bool GiveWeaponToPawn(APawn* InPawn, UClass* WeaponClass);

	// --- Combat ------------------------------------------------------------------------------------------------

	/**
	 * Client -> server: this player's weapon fired (AFPSRLProjectile's local copy asks). The server validates (shooter
	 * up, sane position and rate) and spawns the replicated projectile for everyone else.
	 */
	UFUNCTION(Server, Reliable)
	void ServerFireProjectile(TSubclassOf<AFPSRLProjectile> ProjectileClass, FVector_NetQuantize10 Location, FRotator Rotation,
		const TArray<FString>& SpawnSettings);

	/** Local: while downed (or dead) the weapon inputs (shoot, aim, reload, melee, dash) are removed. */
	void SetWeaponInputBlocked(bool bBlocked);

	/** True when there is at least one player and every player is ready with a complete loadout (a weapon).
	 *  Exec node to match the old BP function. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Lobby")
	bool AreAllPlayersReady() const;

	// --- Blessing selection ------------------------------------------------------------------------------------

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerChooseAspect(int32 EventId, int32 OptionIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerChooseSlot(int32 EventId, int32 OptionIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerAltarBack(int32 EventId);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerSelectBoon(int32 EventId, int32 OptionIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerRerollBoons(int32 EventId);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Boons")
	void ServerSelectUpgrade(int32 EventId, int32 OptionIndex);

	/** Client: the owning player's saved Soul Fragments balance (sent once per PlayerState). */
	UFUNCTION(Server, Reliable)
	void ServerReportTalentEssence(int32 Amount);

	/** Server -> owning client: persist the new balance to the local save file. */
	UFUNCTION(Client, Reliable)
	void ClientPersistentCurrencyChanged(int32 NewAmount);

	/** Local: show this player's pending Blessing or Upgrade choice (reopens it after Close). False if nothing is pending. */
	UFUNCTION(BlueprintCallable, Category = "Boons")
	bool OpenBoonSelection();

	/** Local: this player has a Blessing or Upgrade choice waiting to be made. */
	UFUNCTION(BlueprintPure, Category = "Boons")
	bool HasPendingBoonSelection() const;

	/** Local: the player used a Blessing or Upgrade altar. Reopens an open choice, or asks the server for this altar's. */
	void UseBoonAltar(AFPSRLBoonTerminal* Altar);

	UFUNCTION(Server, Reliable)
	void ServerUseBoonAltar(AFPSRLBoonTerminal* Altar);

	/** Server -> owning client: the altar refused (locked, out of range, already used, nothing to offer). Reason is
	 *  shown briefly on screen when not empty, e.g. "Nothing left to offer". */
	UFUNCTION(Client, Reliable)
	void ClientBoonAltarRejected(const FText& Reason);

	/** Local: a short message on screen (in the revive bar's place), e.g. "Nothing left to offer". */
	void ShowNotice(const FText& Message);

	// --- Expedition rooms ---------------------------------------------------------------------------------------

	/** Client -> server: this machine has loaded and shown room PlacementIndex of the Depth (gates wait for everyone). */
	UFUNCTION(Server, Reliable)
	void ServerReportRoomShown(int32 PlacementIndex);

	/** Local: show, update or hide the Elite / Final Level Boss health bar for this room's encounter. */
	void RefreshEncounterBar(class AFPSRLRoom* Room);

	// --- Death ---------------------------------------------------------------------------------------------------

	/** Server -> owning client: your pawn died. Fades to black, then shows the death menu. */
	UFUNCTION(Client, Reliable)
	void ClientShowDeathScreen(bool bCanReturnToLobby);

	/** Server -> client: the whole party is down, so Return to Lobby now ends the run. */
	UFUNCTION(Client, Reliable)
	void ClientPartyDown();

	/** Death menu's Return to Lobby. Ends the run for everyone, but only once every player is down. */
	UFUNCTION(Server, Reliable)
	void ServerReturnToLobbyAfterDeath();

	/** Pressed E on a downed teammate. The server validates and runs the revive (AFPSRLReviveMarker). */
	UFUNCTION(Server, Reliable)
	void ServerStartRevive(AFPSRLReviveMarker* Marker);

	/** Local: show the revive bar (you are reviving someone, or someone is reviving you). Server world times. */
	void ShowReviveProgress(const FText& Label, double StartTime, double EndTime);

	/** Local: hide the revive bar, optionally flashing a short message first ("Revive interrupted"). */
	void HideReviveProgress(const FText& Message = FText::GetEmpty());

	// --- Exit portal ---------------------------------------------------------------------------------------------

	/** Local: the player interacted with an active exit portal. Shows the Continue / Cancel menu. */
	void OpenPortalMenu(AFPSRLExitPortal* Portal);

	/** Local: the portal's votes or state changed. Updates the open menu and the HUD tracker. */
	void RefreshPortalUI(AFPSRLExitPortal* Portal);

	UFUNCTION(Server, Reliable)
	void ServerPortalVote(AFPSRLExitPortal* Portal, bool bContinue);

	/**
	 * Test commands (development builds, any player; the server runs them for the caller). Console:
	 *  FPSRLGiveBoon DA_Boon_X [Primary|Secondary|Ability]  - grant a Blessing (asset name or path)
	 *  FPSRLOfferBoon     - open a Blessing altar choice without an altar
	 *  FPSRLOfferUpgrade  - open an Upgrade Altar choice without an altar
	 *  FPSRLGiveRelic [DA_Relic_X]  - grant that relic, or roll a random one by rarity
	 *  FPSRLPick N        - pick option N (0-based) at the current step (Aspect, slot, Blessing or upgrade)
	 *  FPSRLReroll        - reroll the Aspect choices (same rules and cost as the button)
	 *  FPSRLBack          - step back at a Blessing altar (Blessing -> slot -> Aspect)
	 */
	UFUNCTION(Exec)
	void FPSRLGiveBoon(const FString& BoonAsset, const FString& Channel);

	UFUNCTION(Exec)
	void FPSRLOfferBoon();

	UFUNCTION(Exec)
	void FPSRLOfferUpgrade();

	UFUNCTION(Exec)
	void FPSRLGiveRelic(const FString& RelicAsset);

	UFUNCTION(Exec)
	void FPSRLPick(int32 OptionIndex);

	UFUNCTION(Exec)
	void FPSRLReroll();

	UFUNCTION(Exec)
	void FPSRLBack();

	/** Listen host, in the Lobby: play a whole run by itself (UFPSRLAutopilotSubsystem). FPSRLAutoRun 1 also tests a fall. Test only. */
	UFUNCTION(Exec)
	void FPSRLAutoRun(int32 TestFall = 0, int32 MinPlayers = 1);

	/** Listen host, in the Lobby: once MinPlayers are in, check that teammates never get identical Aspect choices. Test only. */
	UFUNCTION(Exec)
	void FPSRLAltarTest(int32 MinPlayers = 2);

	/** Playtesting: god mode on/off for this player (also F6). Not in Shipping. */
	UFUNCTION(Exec)
	void FPSRLGod();

	/** Server -> this player: a short message on screen. */
	UFUNCTION(Client, Reliable)
	void ClientShowNotice(const FText& Message);

	/** Runs a test command on the server for this player. Does nothing in Shipping builds. */
	UFUNCTION(Server, Reliable)
	void ServerTestCommand(FName Command, const FString& Arg1, const FString& Arg2);

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
	virtual void AcknowledgePossession(APawn* InPawn) override;
	virtual void OnRep_PlayerState() override;

	/** Blessing / Upgrade selection screen (default C++ layout; set a Blueprint subclass to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Boons")
	TSubclassOf<UFPSRLSelectionWidget> BoonSelectionClass;

	/** Death screen (default C++ layout; set a Blueprint subclass to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Death")
	TSubclassOf<UFPSRLDeathMenuWidget> DeathMenuClass;

	/** Seconds of fade to black before the death menu appears. */
	UPROPERTY(EditDefaultsOnly, Category = "Death", meta = (ClampMin = "0"))
	float DeathFadeSeconds = 1.5f;

	/** Exit portal dialog and HUD vote tracker (default C++ layouts; set Blueprint subclasses to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Portal")
	TSubclassOf<UFPSRLPortalMenuWidget> PortalMenuClass;

	UPROPERTY(EditDefaultsOnly, Category = "Portal")
	TSubclassOf<UFPSRLPortalStatusWidget> PortalStatusClass;

	/** Elite / Final Level Boss health bar (default C++ layout; set a Blueprint subclass to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Encounter")
	TSubclassOf<class UFPSRLEncounterBarWidget> EncounterBarClass;

	/** Revive progress bar (default C++ layout; set a Blueprint subclass to restyle). */
	UPROPERTY(EditDefaultsOnly, Category = "Revive")
	TSubclassOf<UFPSRLReviveWidget> ReviveWidgetClass;

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

	/** Removed while the player is downed: shoot, aim, reload, melee, dash (the template's IMC_Weapons). */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TSoftObjectPtr<UInputMappingContext> WeaponMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Variant_Shooter/Input/IMC_Weapons.IMC_Weapons")));

	/** Server: fastest a client may fire (seconds between projectiles); requests faster than this are dropped. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	float MinClientShotInterval = 0.04f;

	/** Server: how far from the shooter a client-fired projectile may start (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	float MaxClientShotOriginDistance = 400.f;

	/** Widget to show. Defaults to the C++ class, which builds its own layout; set a Blueprint subclass to restyle. */
	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	TSubclassOf<UFPSRLPauseMenuWidget> PauseMenuClass;

private:
	/** Server: give the pawn the weapon from this player's PlayerState. */
	void EquipSelectedWeapon(APawn* InPawn);

	/** Server: time of the last projectile fired for this client (rate sanity check). */
	double LastServerShotTime = -1.0;

	/** Local: weapon inputs are currently removed (downed); priority to restore them at. */
	bool bWeaponInputBlocked = false;
	int32 BlockedWeaponContextPriority = 0;

	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	bool IsInLobby() const;

	// Local selection UI
	void BindToPlayerStateComponents();
	UFUNCTION() void RefreshBoonSelectionUI();
	void ShowSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget, TSubclassOf<UFPSRLSelectionWidget> WidgetClass);
	void HideSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget);
	void HandleBoonChoice(int32 OptionIndex);
	void HandleBoonReroll();
	void HandleBoonClose();
	void HandleBoonBack();

	void ClosePortalMenu();
	void HandlePortalChoice(bool bContinue);

	/** Server: this controller's pawn died (bound in OnPossess). */
	UFUNCTION()
	void HandlePawnDied(AController* Killer, AActor* Causer);

	void ShowDeathMenu();
	static bool IsAnyPlayerAlive(const UWorld* World);

	/** Some modal screen (Blessing, portal, death) is up: keep UI input and the cursor. */
	bool IsAnyModalOpen() const;

	/** Local: the player opened the Blessing screen at an altar (cleared when they close it or the choice resolves). */
	bool bBoonSelectionOpen = false;

	// Local profile bridge (BP_GameInstanceBase.CurrentPlayerProfile.TalentEssence) until the save game moves to C++.
	bool ReadLocalTalentEssence(int32& OutAmount) const;
	void WriteLocalTalentEssence(int32 Amount) const;
	void ReportTalentEssenceIfNeeded();

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLSelectionWidget> BoonSelectionWidget;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPortalMenuWidget> PortalMenu;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPortalStatusWidget> PortalStatus;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLReviveWidget> ReviveWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UFPSRLEncounterBarWidget> EncounterBar;

	/** The room whose encounter the bar shows. */
	TWeakObjectPtr<class AFPSRLRoom> EncounterBarRoom;

	/** Portal the open menu belongs to. */
	TWeakObjectPtr<AFPSRLExitPortal> MenuPortal;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLDeathMenuWidget> DeathMenu;

	FTimerHandle DeathFadeTimer;
	bool bDeathCanReturn = false;

	TWeakObjectPtr<APlayerState> BoundPlayerState;
	TWeakObjectPtr<APlayerState> ReportedPlayerState;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLPauseMenuWidget> PauseMenu;

	/** Input state before the menu opened, restored on close. */
	bool bCursorWasVisible = false;

	FDelegateHandle DestroySessionHandle;
};
