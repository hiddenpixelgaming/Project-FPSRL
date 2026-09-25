// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLInteractionStation.generated.h"

class ACharacter;

/**
 * Base for walk-up interactables (weapon station, start-game console). BP_WeaponStation and BP_StartGameConsole
 * derive from this.
 *
 * C++ decides WHEN: it watches actor-level overlap from whatever collision the station has (e.g. a proximity sphere)
 * and raises On Interactor Entered / Left when the local player walks in and out. By default those show and clear
 * the character's "Press E" prompt (a bridge to BP_ShooterCharacter's SetCurrentInteractable /
 * ClearCurrentInteractable until the character moves to C++ in Step F), so a new station needs no Blueprint graph;
 * a Blueprint may override them. Pressing E calls Interact on the station. Event-driven, no Tick.
 *
 * Local-only: prompts are UI for the player on this machine, so only the locally controlled player character
 * raises the events. Each player tracks their own station, so several players can stand at the same station.
 */
UCLASS(Abstract)
class FPSRL_API AFPSRLInteractionStation : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLInteractionStation();

	/** Prompt shown while the local player is in range, e.g. "Press E to Access Weapon Station". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FString PromptText;

	/**
	 * The local player pressed interact at this station (called by the character's interact handling).
	 * Default does nothing; stations whose menu lives in C++ (e.g. AFPSRLBoonTerminal) override it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(APlayerController* User);

	/** Whether the station can be used right now. Locked stations show no prompt. Default: always usable. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	virtual bool CanInteract() const { return true; }

	/** Call when CanInteract() changes: shows/hides the prompt for a local player already standing in range. */
	void RefreshLocalInteractor();

protected:
	/**
	 * The local player entered interaction range. Default: show PromptText on the character's interact prompt.
	 * A Blueprint override replaces the default.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction", meta = (DisplayName = "On Interactor Entered"))
	void K2_OnInteractorEntered(ACharacter* Interactor);

	/** The local player left interaction range. Default: clear the character's interact prompt. */
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction", meta = (DisplayName = "On Interactor Left"))
	void K2_OnInteractorLeft(ACharacter* Interactor);

	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;

private:
	static ACharacter* AsLocalPlayerCharacter(AActor* Actor);

	/** Local character currently shown this station's prompt (so Entered/Left always pair up). */
	TWeakObjectPtr<ACharacter> PromptedCharacter;
};
