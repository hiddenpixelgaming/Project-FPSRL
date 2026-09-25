// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Rooms/FPSRLInteractionStation.h"
#include "FPSRLReviveMarker.generated.h"

class APawn;
class APlayerController;
class APlayerState;
class USphereComponent;

/**
 * "Press E to Revive" station that follows a downed player (spawned by UFPSRLHealthComponent, server-only).
 *
 * A teammate who is up presses E beside the body: the server starts a ReviveSeconds revive, re-checked every 0.2 s.
 * If the reviver goes down, dies, or moves out of range, the revive is cancelled (press E again to restart).
 * On completion the downed player gets back up with ReviveHealthFraction of their max health.
 * Only one teammate revives at a time; the downed player never sees their own prompt. Event-driven, no Tick.
 */
UCLASS(NotPlaceable)
class FPSRL_API AFPSRLReviveMarker : public AFPSRLInteractionStation
{
	GENERATED_BODY()

public:
	AFPSRLReviveMarker();

	/** Server: attach to the downed pawn. */
	void SetTarget(APawn* InTarget);

	/** Server: a teammate pressed E. False if they can't revive right now. */
	bool TryStartRevive(APlayerController* Reviver);

	virtual bool CanInteract() const override;
	virtual void Interact_Implementation(APlayerController* User) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Revive")
	TObjectPtr<USphereComponent> ReviveRange;

private:
	UFUNCTION()
	void HandleTargetDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_Revive();

	void CheckRevive();
	void CancelRevive();

	UPROPERTY(Replicated)
	TObjectPtr<APawn> Target;

	/** The teammate currently reviving (null = nobody). */
	UPROPERTY(ReplicatedUsing = OnRep_Revive)
	TObjectPtr<APlayerState> Reviver;

	/** Server world time the revive completes. */
	UPROPERTY(ReplicatedUsing = OnRep_Revive)
	double ReviveEndTime = 0.0;

	FTimerHandle ReviveTimer;
};
