// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLInteractionStation.h"
#include "FPSRLBoonTerminal.generated.h"

class AFPSRLPlayerController;
class AFPSRLPlayerState;
class AFPSRLRoom;
class APlayerState;
class USphereComponent;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Blessing altar: an optional walk-up terminal where each player gets ONE personal Blessing choice.
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

	/** Server: give this player their choice. False if locked, out of range, already used, or nothing to offer;
	 *  OutReason then holds what to tell the player (empty = say nothing). */
	bool TryOffer(AFPSRLPlayerController* PC, FText& OutReason);

	/** Local: a Blessing altar looks and reads like an Upgrade Altar for a player who has taken every Blessing they can
	 *  (their Blessing altars upgrade instead for the rest of the run). Called when this player's build changes. */
	virtual void RefreshLocalAppearance();

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool IsUnlocked() const { return bUnlocked; }

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool HasBeenUsedBy(const APlayerState* Player) const;

	/** Server, before it finishes spawning: the room whose encounter unlocks it (an altar spawned into a combat room). */
	void SetRoom(AFPSRLRoom* InRoom) { Room = InRoom; }

	/** Local: normal colours while this machine's player can use it, a darker tint otherwise (locked until its room is
	 *  cleared, or already used). Uses the mesh material's "Color" parameter; materials without one keep their look. */
	void RefreshAvailabilityTint();

protected:
	/** Server: open this altar's kind of choice for the player (Blessings, or upgrades once they have every Blessing;
	 *  AFPSRLUpgradeAltar: always upgrades). */
	virtual bool BeginPlayerSelection(AFPSRLPlayerState* Player);

public:
	/** Using it opens a choice screen (Blessings, upgrades). False: it acts at once (Healing Altar). */
	virtual bool OpensSelectionScreen() const { return true; }

protected:

	/** What the player is told when BeginPlayerSelection has nothing for them. */
	virtual FText GetNothingToOfferText() const { return NSLOCTEXT("FPSRL", "NothingLeft", "Nothing left to offer"); }

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

	/** Look and prompt this Blessing altar takes for a player who has taken every Blessing (see RefreshLocalAppearance). */
	UPROPERTY(EditDefaultsOnly, Category = "Terminal")
	TSoftObjectPtr<UMaterialInterface> UpgradeLookMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Terminal")
	FString UpgradePromptText = TEXT("Press E to Upgrade a Blessing");

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

	/** Local: the altar's own look, restored when it is a Blessing altar again (next run). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalMaterial;

	FString OriginalPromptText;
	bool bShowingUpgradeLook = false;

	/** Local: the tinting instance on the mesh and the colour it had before any tint. */
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> TintMaterial;
	FLinearColor TintBaseColor = FLinearColor::White;
	bool bTintHasColor = false;
};
