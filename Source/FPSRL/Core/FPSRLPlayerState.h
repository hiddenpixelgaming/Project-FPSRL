// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "FPSRLPlayerState.generated.h"

class UAbilitySystemComponent;
class UFPSRLAbilitySystemComponent;
class UFPSRLAspectComponent;
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
 *    run build (weapon, aspect, boons) across; BeginRunState re-grants it through the new ASC.
 *
 * Replication: the ASC replicates in Mixed mode. The owning client gets full Gameplay Effect data (for its own HUD
 * and prediction); other clients get only tags and cues. Server is authoritative for every attribute change.
 *
 * Lobby state: bIsReady and SelectedWeapon replicate to everyone (the lobby list shows who is ready). Both are set
 * only by the server, via AFPSRLPlayerController's Server RPCs. SelectedWeapon is copied to the new PlayerState on
 * the Lobby -> Arena seamless travel (CopyProperties), which is what carries each player's own choice into the run.
 *
 * Run state (temporary): BoonComponent (boons) and AspectComponent (aspect). Cleared by ClearRunState() when the
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
	UFPSRLAspectComponent* GetAspectComponent() const { return AspectComponent; }

	/** Lobby ready flag. Not carried across travel: everyone starts the next lobby visit un-ready. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bIsReady = false;

	/** Lobby-selected weapon (a Weapon.* tag). Carried across travel into the run. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	FGameplayTag SelectedWeapon;

	/** Soul Fragments / Talent Essence: persistent currency (owner only). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Currency")
	int32 TalentEssence = 0;

	/** Server-only setters (called by AFPSRLPlayerController's RPCs). */
	void SetIsReady(bool bNewReady);
	void SetSelectedWeapon(const FGameplayTag& NewWeapon);

	/** Server: accepts the owning client's saved balance once per PlayerState. */
	void ReceiveReportedTalentEssence(int32 Amount);

	/** Server: deducts if affordable and persists. Returns false (no change) if not. */
	bool TrySpendTalentEssence(int32 Amount);

	/** Server: grants currency (bosses, chests) and persists. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Currency")
	void AddTalentEssence(int32 Amount);

	/** Weapon chosen and (if the weapon has any) an aspect chosen: required before the host can start. */
	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool HasCompletedLoadout() const;

	/** Server: entering a Depth. Applies the aspect and re-grants carried boons (idempotent) and marks the run started. */
	void BeginRunState();

	/** Server: run is over. Removes only temporary Boon/Aspect grants and state; safe to call more than once. */
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

	void PersistTalentEssence();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boons", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLBoonComponent> BoonComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aspect", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLAspectComponent> AspectComponent;

	/** Player health attributes. Lives here (not on the pawn) so run effects on health survive respawn. */
	UPROPERTY()
	TObjectPtr<UFPSRLHealthSet> HealthSet;

	/** Combat stats boons/aspects modify through Gameplay Effects. */
	UPROPERTY()
	TObjectPtr<UFPSRLCombatSet> CombatSet;

	/** Capacities such as MaxBoonSlots (Talent Tree raises them). */
	UPROPERTY()
	TObjectPtr<UFPSRLProgressionSet> ProgressionSet;

	/** Server: a run has started for this player (carried across travel so the Lobby knows to clean up). */
	bool bRunStateActive = false;
	bool bTalentEssenceReported = false;
};
