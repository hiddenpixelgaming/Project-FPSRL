// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLExitPortal.generated.h"

class APlayerState;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * The Depth's exit. BP_ExitPortal derives from this (mesh, VFX/SFX on state change).
 *
 * State machine (server-driven, replicated): Hidden/Locked until the Depth's required encounters are done
 * (AFPSRLGameState::OnDepthCompleted) -> Activating for PortalActivationDelay -> Active -> Used (travel) .
 * The portal never ends the Depth by itself: the party decides when to leave, after looting, altars, merchants.
 *
 * Multiplayer: standing in the portal = ready. When every living player is ready, the party travels at once.
 * Otherwise the first ready player starts a countdown (PortalCountdownSeconds, 0 = wait for all); when it ends the
 * whole party travels (seamless travel takes everyone along). If everyone steps back out, the countdown stops,
 * so one player can't end the Depth for teammates who are still exploring. Dead players never block it.
 *
 * Travel goes forward only (UFPSRLRunSubsystem::AdvanceRun): the next Depth, the next Area, or back to the Lobby
 * after the last Depth. Event-driven, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLExitPortal : public AActor
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

	/** Players currently standing in the portal. */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyPlayers, BlueprintReadOnly, Category = "Portal")
	TArray<TObjectPtr<APlayerState>> ReadyPlayers;

	/** Living players the portal is waiting for (ready or not). */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyPlayers, BlueprintReadOnly, Category = "Portal")
	int32 LivingPlayers = 0;

	/** Server world time the countdown ends; 0 = no countdown running. */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyPlayers, BlueprintReadOnly, Category = "Portal")
	double CountdownEndTime = 0.0;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** State changed (server and clients): play activation VFX/SFX, show "Depth cleared", etc. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Portal", meta = (DisplayName = "On Portal State Changed"))
	void K2_OnPortalStateChanged(EPortalState NewState);

	/** Ready count or countdown changed (server and clients). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Portal", meta = (DisplayName = "On Ready Players Changed"))
	void K2_OnReadyPlayersChanged(int32 ReadyCount, int32 LivingCount);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Step in here to be ready. Only collides with pawns, and only while Active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UBoxComponent> EntryZone;

private:
	UFUNCTION()
	void HandleDepthCompleted();

	UFUNCTION()
	void OnEntryBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnEntryEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UFUNCTION()
	void OnRep_PortalState();

	UFUNCTION()
	void OnRep_ReadyPlayers();

	void SetPortalState(EPortalState NewState);
	void ApplyPortalState();
	void Activate();
	void RefreshReadyPlayers();
	void EvaluateReady();
	void Travel();

	static APlayerState* GetLivingPlayerState(AActor* Actor);

	FTimerHandle ActivationTimer;
	FTimerHandle CountdownTimer;
};
