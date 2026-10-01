// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "FPSRLPlayerState.generated.h"

class UAbilitySystemComponent;
class UFPSRLAbilitySystemComponent;
class UFPSRLRelicComponent;
class UFPSRLBoonComponent;
class UFPSRLCombatSet;
class UFPSRLHealthSet;
class UFPSRLProgressionSet;

/**
 * Base PlayerState for every FPSRL level (Lobby and Match). BP_PlayerStateLobby derives from this.
 *
 * Owns the player's Ability System Component (ASC). The ASC lives here rather than on the Character because:
 *  - The PlayerState outlives the pawn, so run-scoped boons and effects survive death and respawn.
 *  - Each Depth is its own map: seamless travel creates a new PlayerState per Depth, and CopyProperties hands the
 *    run build (weapon, Blessings, relics) and current health across; BeginRunState re-grants it through the new ASC.
 *    Health is only restored to full in the Lobby.
 *
 * Replication: the ASC replicates in Mixed mode. The owning client gets full Gameplay Effect data (for its own HUD
 * and prediction); other clients get only tags and cues. Server is authoritative for every attribute change.
 *
 * Lobby state: bIsReady and SelectedWeapon replicate to everyone (the lobby list shows who is ready). Both are set
 * only by the server, via AFPSRLPlayerController's Server RPCs. SelectedWeapon is copied to the new PlayerState on
 * the Lobby -> Arena seamless travel (CopyProperties), which is what carries each player's own choice into the run.
 *
 * Run state (temporary): BoonComponent (Blessings on the Primary / Secondary / Ability channels) and RelicComponent. Cleared by ClearRunState() when the
 * player is back in the Lobby after a run; only effects tagged Effect.Temporary.Run are ever removed, so permanent
 * progression (Talent Tree, tagged Effect.Permanent.Talent) is untouched.
 *
 * Persistent currency: TalentEssence (= Soul Fragments). The owning client's save file is the long-term store; the
 * client reports it once per PlayerState and the server is authoritative for the session (rerolls, rewards), writing
 * changes back to the client's save via AFPSRLPlayerController::ClientPersistentCurrencyChanged.
 */
UCLASS()
class FPSRL_API AFPSRLPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRLPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFPSRLBoonComponent* GetBoonComponent() const { return BoonComponent; }
	UFPSRLRelicComponent* GetRelicComponent() const { return RelicComponent; }

	/** Lobby ready flag. Not carried across travel: everyone starts the next lobby visit un-ready. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bIsReady = false;

	/** Playtesting (F1 / FPSRLGod, not in Shipping): no damage, can't die, falls put you back on the floor. Kept for the run. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Test")
	bool bGodMode = false;

	/** Lobby-selected weapon (a Weapon.* tag). Carried across travel into the run. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	FGameplayTag SelectedWeapon;

	/**
	 * Weapon the server has put in this player's current pawn's hands (empty = none yet, e.g. a fresh Lobby pawn).
	 * Weapons are local actors, so every client gives the pawn the same weapon itself when this replicates.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_EquippedWeapon, BlueprintReadOnly, Category = "Lobby")
	FGameplayTag EquippedWeapon;

	/** Secondary-channel item (Secondary.*; the built-in melee by default). Blessings check it by tag. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Loadout")
	FGameplayTag SecondaryItem;

	/** Ability-channel item (Ability.*). Empty until a real ability exists, so the channel is never offered. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Loadout")
	FGameplayTag AbilityItem;

	/** Soul Fragments / Talent Essence: persistent currency (owner only). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Currency")
	int32 TalentEssence = 0;

	/** Which room of the Depth's sequence this player is in (server-tracked from where their pawn is, INDEX_NONE = the
	 *  Depth's entry map or not tracked yet). Players may be in different rooms; see UFPSRLDepthLayoutComponent. A new
	 *  Depth starts at INDEX_NONE (not copied across travel). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Expedition")
	int32 ExpeditionRoomIndex = INDEX_NONE;

	/** Server-only setters (called by AFPSRLPlayerController's RPCs). */
	void SetIsReady(bool bNewReady);
	void SetSelectedWeapon(const FGameplayTag& NewWeapon);
	void SetEquippedWeapon(const FGameplayTag& NewWeapon);

	/** Server: accepts the owning client's saved balance once per PlayerState. */
	void ReceiveReportedTalentEssence(int32 Amount);

	/** Server: deducts if affordable and persists. Returns false (no change) if not. */
	bool TrySpendTalentEssence(int32 Amount);

	/** Server: grants currency (bosses, chests) and persists. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Currency")
	void AddTalentEssence(int32 Amount);

	/** Weapon chosen: required before the host can start. */
	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool HasCompletedLoadout() const;

	/** Server: entering a Depth. Re-grants the carried Blessings and relics (idempotent) and marks the run started. */
	void BeginRunState();

	/** Server: run is over. Removes only temporary Blessing / relic grants and state; safe to call more than once. */
	void ClearRunState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

protected:
	virtual void PostInitializeComponents() override;

private:
	/**
	 * Keeps the ASC's avatar (the actor abilities act through) pointed at the current pawn.
	 * Fires on the server when the pawn is possessed and on clients when the pawn's PlayerState replicates,
	 * so both sides initialize without a C++ Character (see APawn::SetPlayerState).
	 */
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);

	/** Pawn walk speed = its class default x MoveSpeedMultiplier (skipped while downed: the crawl owns the speed then). */
	void ApplyMoveSpeed();
	void HandleMoveSpeedChanged(const struct FOnAttributeChangeData& ChangeData) { ApplyMoveSpeed(); }

	void PersistTalentEssence();

	UFUNCTION()
	void OnRep_EquippedWeapon();

	/** Client: give the current pawn EquippedWeapon locally (once per pawn and weapon; waits for the pawn's BeginPlay). */
	void EquipWeaponLocally();

	/** Someone else's pawn on a client (simulated proxy): stop animating its first-person arms (only its own player sees them). */
	void StopFirstPersonAnimationIfRemote(APawn* InPawn) const;

	TWeakObjectPtr<APawn> LocallyEquippedPawn;
	FGameplayTag LocallyEquippedWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boons", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLBoonComponent> BoonComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Relics", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLRelicComponent> RelicComponent;

	/** Player health attributes. Lives here (not on the pawn) so run effects on health survive respawn. */
	UPROPERTY()
	TObjectPtr<UFPSRLHealthSet> HealthSet;

	/** Combat stats Blessings, Aspects and relics modify through Gameplay Effects. */
	UPROPERTY()
	TObjectPtr<UFPSRLCombatSet> CombatSet;

	/** Capacities such as MaxBoonSlots (Talent Tree raises them). */
	UPROPERTY()
	TObjectPtr<UFPSRLProgressionSet> ProgressionSet;

	/** Server: a run has started for this player (carried across travel so the Lobby knows to clean up). */
	bool bRunStateActive = false;
	bool bTalentEssenceReported = false;

	/** Server: health when the previous Depth was left, applied by BeginRunState; -1 = none. */
	float CarriedHealth = -1.f;
};
