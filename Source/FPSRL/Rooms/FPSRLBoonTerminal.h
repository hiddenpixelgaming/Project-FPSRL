// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLInteractionStation.h"
#include "FPSRLBoonTerminal.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * Walk-up terminal where a player makes their pending Boon choice after a room is cleared.
 * BP_BoonTerminal derives from this (mesh, prompt hookup).
 *
 * Rule: the terminal is LOCKED until every enemy in its room is dead. Each placed terminal is linked to its room's
 * arena manager (Arena); the server unlocks it when that arena reports completion (its OnArenaComplete event, which
 * fires after the last enemy dies and the Boon selection has started) and locks it again once the selection is over.
 * The locked state replicates; a locked terminal shows no prompt and ignores interaction.
 *
 * Using it only opens the local player's own selection screen (AFPSRLPlayerController::OpenBoonSelection); the choice
 * itself is validated and timed out by the server (AFPSRLGameState), so a player who never reaches the terminal is
 * auto-picked and can never block the group. Any number of players can use it; each sees only their own options.
 */
UCLASS()
class FPSRL_API AFPSRLBoonTerminal : public AFPSRLInteractionStation
{
	GENERATED_BODY()

public:
	AFPSRLBoonTerminal();

	virtual void Interact_Implementation(APlayerController* User) override;
	virtual bool CanInteract() const override { return bUnlocked; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The arena manager of this terminal's room. Required: an unlinked terminal never unlocks. */
	UPROPERTY(EditInstanceOnly, Category = "Terminal")
	TObjectPtr<AActor> Arena;

	/**
	 * Name of the arena's no-parameter "room cleared" event dispatcher.
	 * Temporary reflection bridge while the arena manager is still a Blueprint (Step D3 moves rooms to C++).
	 */
	UPROPERTY(EditAnywhere, Category = "Terminal")
	FName ArenaCompleteEvent = TEXT("OnArenaComplete");

	/** Locked/unlocked changed (server and clients); use for visuals such as a lit screen. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Terminal", meta = (DisplayName = "On Unlocked Changed"))
	void K2_OnUnlockedChanged(bool bIsUnlocked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Walk-in range for the interact prompt. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<USphereComponent> InteractionRange;

private:
	/** Server: the linked arena was cleared. */
	UFUNCTION()
	void HandleArenaCleared();

	/** Server: every player has resolved this selection. */
	UFUNCTION()
	void HandleSelectionComplete();

	void SetUnlocked(bool bNewUnlocked);

	UFUNCTION()
	void OnRep_Unlocked();

	UPROPERTY(ReplicatedUsing = OnRep_Unlocked)
	bool bUnlocked = false;
};
