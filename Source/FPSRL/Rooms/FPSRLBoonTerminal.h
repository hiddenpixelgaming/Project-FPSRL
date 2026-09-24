// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLInteractionStation.h"
#include "FPSRLBoonTerminal.generated.h"

class AFPSRLPlayerController;
class AFPSRLRoom;
class APlayerState;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Boon altar: an optional walk-up terminal where each player gets ONE personal boon choice.
 * BP_BoonTerminal derives from this (mesh, prompt hookup).
 *
 * Locked until its Room's enemies are all dead (a terminal without a Room is open from the start, e.g. in a reward
 * room). Once open, every player may use it once: the server rolls that player's own options (UFPSRLBoonComponent),
 * and the player picks, rerolls, or closes the screen and comes back later. Nothing waits for it: the Depth's exit
 * portal opens regardless, and a choice still open when the party leaves the Depth is forfeited.
 *
 * Server-authoritative: the client only asks (AFPSRLPlayerController::ServerUseBoonAltar); the server checks the
 * altar is open, the player is in range and hasn't used it yet. ClaimedBy replicates so each client hides the prompt
 * once it has used the altar.
 */
UCLASS()
class FPSRL_API AFPSRLBoonTerminal : public AFPSRLInteractionStation
{
	GENERATED_BODY()

public:
	AFPSRLBoonTerminal();

	virtual void Interact_Implementation(APlayerController* User) override;

	/** Local: open, and this machine's player either hasn't used it or still has its choice open. */
	virtual bool CanInteract() const override;

	/** Server: give this player their choice. False if locked, out of range, already used, or nothing to offer. */
	bool TryOffer(AFPSRLPlayerController* PC);

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool IsUnlocked() const { return bUnlocked; }

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool HasBeenUsedBy(const APlayerState* Player) const;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The room whose encounter must be cleared first. None = open from the start. */
	UPROPERTY(EditInstanceOnly, Category = "Terminal")
	TObjectPtr<AFPSRLRoom> Room;

	/** Locked/unlocked changed (server and clients); use for visuals such as a lit screen. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Terminal", meta = (DisplayName = "On Unlocked Changed"))
	void K2_OnUnlockedChanged(bool bIsUnlocked);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Walk-in range for the interact prompt. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<USphereComponent> InteractionRange;

private:
	UFUNCTION()
	void HandleRoomCompleted();

	void SetUnlocked(bool bNewUnlocked);

	UFUNCTION()
	void OnRep_Unlocked();

	UFUNCTION()
	void OnRep_ClaimedBy();

	UPROPERTY(ReplicatedUsing = OnRep_Unlocked)
	bool bUnlocked = false;

	/** Players who have used this altar. */
	UPROPERTY(ReplicatedUsing = OnRep_ClaimedBy)
	TArray<TObjectPtr<APlayerState>> ClaimedBy;
};
