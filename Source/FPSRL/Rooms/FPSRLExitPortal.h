// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLInteractionStation.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLExitPortal.generated.h"

class APlayerController;
class APlayerState;
class USphereComponent;
class UStaticMeshComponent;

/**
 * The Depth's exit. BP_ExitPortal derives from this (mesh, prompt hookup, VFX/SFX on state change).
 *
 * State machine (server-driven, replicated): Hidden/Locked until the Depth's required encounters are done
 * (AFPSRLGameState::OnDepthCompleted) -> Activating for PortalActivationDelay -> Active -> Used (travel).
 * The portal never ends the Depth by itself: the party decides when to leave, after looting, altars, merchants.
 *
 * Leaving is a vote. Interacting while Active opens a Continue / Cancel menu (AFPSRLPlayerController). Continue
 * registers the player, Cancel withdraws. Every living player continuing -> the party travels at once (solo: at once).
 * At PortalVoteThreshold (50%) or more of the living players, a PortalCountdownSeconds (35 s) countdown starts; when it
 * ends the whole party travels together (seamless travel takes everyone along). Dropping below the threshold stops it.
 * Dead players are not counted, so they never block the vote.
 *
 * Travel goes forward only (UFPSRLRunSubsystem::AdvanceRun): the next Depth, the next Area, or back to the Lobby
 * after the last Depth. Event-driven, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLExitPortal : public AFPSRLInteractionStation
{
	GENERATED_BODY()

public:
	AFPSRLExitPortal();

	/** Show the (inert) portal before the Depth is complete instead of hiding it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	bool bVisibleWhileLocked = false;

	UPROPERTY(ReplicatedUsing = OnRep_PortalState, BlueprintReadOnly, Category = "Portal")
	EPortalState PortalState = EPortalState::Hidden;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Portal")
	EPortalDestination Destination = EPortalDestination::RunComplete;

	/** Players who chose Continue. */
	UPROPERTY(ReplicatedUsing = OnRep_Votes, BlueprintReadOnly, Category = "Portal")
	TArray<TObjectPtr<APlayerState>> ContinueVotes;

	/** Living players the vote counts. */
	UPROPERTY(ReplicatedUsing = OnRep_Votes, BlueprintReadOnly, Category = "Portal")
	int32 LivingPlayers = 0;

	/** Server world time the countdown ends; 0 = no countdown running. */
	UPROPERTY(ReplicatedUsing = OnRep_Votes, BlueprintReadOnly, Category = "Portal")
	double CountdownEndTime = 0.0;

	virtual bool CanInteract() const override { return PortalState == EPortalState::Active; }
	virtual void Interact_Implementation(APlayerController* User) override;

	UFUNCTION(BlueprintPure, Category = "Portal")
	bool HasVotedToContinue(const APlayerState* Player) const { return Player && ContinueVotes.Contains(Player); }

	/** Server: a player chose Continue (true) or Cancel (false). Ignored unless Active; Continue requires range. */
	void SetContinueVote(APlayerController* Voter, bool bContinue);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** State changed (server and clients): play activation VFX/SFX, show "Depth cleared", etc. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Portal", meta = (DisplayName = "On Portal State Changed"))
	void K2_OnPortalStateChanged(EPortalState NewState);

	/** Vote count or countdown changed (server and clients). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Portal", meta = (DisplayName = "On Votes Changed"))
	void K2_OnVotesChanged(int32 ContinueCount, int32 LivingCount);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Walk-in range for the interact prompt. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<USphereComponent> InteractionRange;

private:
	UFUNCTION()
	void HandleDepthCompleted();

	UFUNCTION()
	void OnRep_PortalState();

	UFUNCTION()
	void OnRep_Votes();

	void HandlePlayerLeft();

	void SetPortalState(EPortalState NewState);
	void ApplyPortalState();
	void Activate();
	void EvaluateVotes();
	void Travel();
	void NotifyLocalPlayer();

	static bool IsLivingPlayer(const APlayerState* Player);

	FTimerHandle ActivationTimer;
	FTimerHandle CountdownTimer;
	FDelegateHandle PlayerLeftHandle;
};
