// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "FPSRLPlayerState.generated.h"

class UAbilitySystemComponent;
class UFPSRLAbilitySystemComponent;
class UFPSRLHealthSet;

/**
 * Base PlayerState for every FPSRL level (Lobby and Match). BP_PlayerStateLobby derives from this.
 *
 * Owns the player's Ability System Component (ASC). The ASC lives here rather than on the Character because:
 *  - The PlayerState outlives the pawn, so run-scoped boons and effects survive death and respawn.
 *  - A run is one streamed level with no map travel, so everything granted during the run lasts until
 *    the travel back to the Lobby, which recreates the PlayerState and wipes it (boons are run-only by design).
 *
 * Replication: the ASC replicates in Mixed mode. The owning client gets full Gameplay Effect data (for its own HUD
 * and prediction); other clients get only tags and cues. Server is authoritative for every attribute change.
 *
 * Lobby state: bIsReady and SelectedWeapon replicate to everyone (the lobby list shows who is ready). Both are set
 * only by the server, via AFPSRLPlayerController's Server RPCs. SelectedWeapon is copied to the new PlayerState on
 * the Lobby -> Arena seamless travel (CopyProperties), which is what carries each player's own choice into the run.
 */
UCLASS()
class FPSRL_API AFPSRLPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRLPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** Lobby ready flag. Not carried across travel: everyone starts the next lobby visit un-ready. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bIsReady = false;

	/** Lobby-selected weapon (a Weapon.* tag). Carried across travel into the run. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	FGameplayTag SelectedWeapon;

	/** Server-only setters (called by AFPSRLPlayerController's RPCs). */
	void SetIsReady(bool bNewReady);
	void SetSelectedWeapon(const FGameplayTag& NewWeapon);

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFPSRLAbilitySystemComponent> AbilitySystemComponent;

	/** Player health attributes. Lives here (not on the pawn) so run effects on health survive respawn. */
	UPROPERTY()
	TObjectPtr<UFPSRLHealthSet> HealthSet;
};
