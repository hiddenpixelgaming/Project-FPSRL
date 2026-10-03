// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSRLEnemyRoleComponent.generated.h"

class UStaticMeshComponent;

/**
 * What players see of an enemy role's attacks, on every machine: the attack's animation (UFPSRLEnemyDefinition::
 * AttackAnims, timed to land when the attack executes) and the aim line telegraph (Marksman's laser).
 *
 * The AI runs on the server only; it calls StartAttack / CancelAttack and this component multicasts them. Added by
 * UFPSRLEnemyBodySubsystem on the server to enemies whose definition has attack animations or an aim-line attack, and
 * replicated to clients. Ticks only while an aim line is showing (it follows the target).
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLEnemyRoleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLEnemyRoleComponent();

	/** Server: an attack starts its wind-up. */
	void StartAttack(FName AttackName, float WindupSeconds, AActor* Target, bool bAimLine);

	/** Server: the attack in progress was cancelled (interrupted, stunned, died). */
	void CancelAttack();

	/** Seconds of the last StartAttack's animation, or 0 (tests). */
	float GetLastAnimLength() const { return LastAnimLength; }

	/** The aim line is on screen (tests). */
	bool IsAimLineShowing() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartAttack(FName AttackName, float WindupSeconds, AActor* Target, bool bAimLine);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastCancelAttack();

	void ShowAimLine(AActor* Target, float Seconds);
	void HideAimLine();
	FVector GetAimLineStart() const;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> AimLine;

	TWeakObjectPtr<AActor> AimTarget;
	double AimLineEndTime = 0.0;
	double AimLineStartTime = 0.0;
	FTimerHandle AimLineTimer;
	float LastAnimLength = 0.f;
};
