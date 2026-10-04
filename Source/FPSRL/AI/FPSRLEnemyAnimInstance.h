// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "FPSRLEnemyAnimInstance.generated.h"

class UAnimSequence;

/** The proxy does the work (worker thread): locomotion (idle / walk / run by speed) plus one full-body action. */
struct FFPSRLEnemyAnimProxy : public FAnimInstanceProxy
{
	FFPSRLEnemyAnimProxy() = default;
	FFPSRLEnemyAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Copied from the instance on the game thread.
	TObjectPtr<UAnimSequence> Idle;
	TObjectPtr<UAnimSequence> Walk;
	TObjectPtr<UAnimSequence> Run;
	float WalkAnimSpeed = 150.f;
	float RunAnimSpeed = 500.f;
	float Speed = 0.f;
	TObjectPtr<UAnimSequence> Action;
	float ActionStart = 0.f;
	float ActionEnd = 0.f;
	float ActionRate = 1.f;
	float ActionBlendIn = 0.12f;
	float ActionBlendOut = 0.2f;
	int32 ActionSerial = 0;
	bool bActionStopping = false;
	bool bFalling = false;

	// Proxy-side state.
	float IdleTime = 0.f;
	float LocoSpeed = 0.f;	// Speed smoothed: crowd avoidance makes it flicker, and the gait blend must not
	float MovePhase = 0.f;	// 0..1, shared by walk and run so their steps line up
	int32 PlayingSerial = 0;
	float ActionTime = 0.f;
	float ActionWeight = 0.f;

private:
	/** One sequence at Time into Pose, in place: the root's travel (Mixamo / Mannequin root motion) is taken out and its
	 *  height and rotation moved onto the pelvis, so the body stays on its capsule whatever the animation does. */
	void Sample(const UAnimSequence* Sequence, float Time, bool bLooping, FPoseContext& Pose) const;
};

/**
 * Animation for the enemy roles (Brute, Skirmisher) on the enemy's hidden Mannequin rig, from C++: walks, runs and idles
 * by its speed (the definition's AnimSet, authored speeds included), and plays one action at a time over the whole body
 * (attacks, the Brute's roar and leap, the Skirmisher's phase leap) with short blends. Every Mixamo / Mannequin
 * animation is played in place. Set as the rig's anim class by UFPSRLEnemyBodySubsystem when the definition has an
 * AnimSet; actions come from UFPSRLEnemyRoleComponent on every machine.
 */
UCLASS(Transient, NotBlueprintable)
class FPSRL_API UFPSRLEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Sets the locomotion directly (players while downed: writhe / crawl), instead of an enemy definition's AnimSet. */
	void SetLocomotion(UAnimSequence* InIdle, UAnimSequence* InWalk, float InWalkAnimSpeed, UAnimSequence* InRun = nullptr, float InRunAnimSpeed = 500.f);

public:
	/** Plays an action from StartTime to EndTime (seconds into the animation; EndTime 0 = its end) at PlayRate. */
	void PlayAction(UAnimSequence* Animation, float StartTime, float EndTime, float PlayRate);

	/** Blends the current action out. */
	void StopAction();

	bool IsPlayingAction() const { return Action != nullptr && !bActionStopping; }

protected:
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FFPSRLEnemyAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Walk;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Run;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Action;

	float WalkAnimSpeed = 150.f;
	float RunAnimSpeed = 500.f;
	float ActionStart = 0.f;
	float ActionEnd = 0.f;
	float ActionRate = 1.f;
	int32 ActionSerial = 0;
	bool bActionStopping = false;
};
