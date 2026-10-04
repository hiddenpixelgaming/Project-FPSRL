// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSRLDownedAnimComponent.generated.h"

class UAnimInstance;

/**
 * A downed player's body as everyone sees it: it falls, writhes on the ground when still and crawls when moving
 * (UFPSRLEnemyAnimInstance with UFPSRLRunSettings' downed animations); revived, it stands up and goes back to its own
 * animation. Every machine, driven by the health component's downed state (which replicates); added on first need.
 * Missing art (not in Git) = nothing changes.
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLDownedAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Downed (true) or revived (false). bDied: it didn't get up (death), no stand-up. */
	void SetDowned(bool bDowned, bool bDied);

	bool IsShowingDowned() const { return bShowingDowned; }

private:
	void Restore();

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> SavedAnimClass;

	FTimerHandle RestoreTimer;
	bool bShowingDowned = false;
};
