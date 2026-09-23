// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "FPSRLPlayerState.generated.h"

class UAbilitySystemComponent;

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
 */
UCLASS()
class FPSRL_API AFPSRLPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSRLPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

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
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
};
