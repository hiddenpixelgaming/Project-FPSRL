// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyAnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Data/FPSRLEnemyScaling.h"
#include "GameFramework/Pawn.h"

void UFPSRLEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	const APawn* Pawn = TryGetPawnOwner();
	const UFPSRLEnemyDefinition* Definition = Pawn ? UFPSRLEnemyScalingSettings::Get().FindDefinition(Pawn->GetClass()) : nullptr;
	if (!Definition)
	{
		return;
	}
	const FFPSRLEnemyAnimSet& Set = Definition->AnimSet;
	Idle = Set.Idle.LoadSynchronous();
	Walk = Set.Walk.LoadSynchronous();
	Run = Set.Run.LoadSynchronous();
	WalkAnimSpeed = FMath::Max(10.f, Set.WalkAnimSpeed);
	RunAnimSpeed = FMath::Max(WalkAnimSpeed + 10.f, Set.RunAnimSpeed);
}

void UFPSRLEnemyAnimInstance::PlayAction(UAnimSequence* Animation, float StartTime, float EndTime, float PlayRate)
{
	if (!Animation)
	{
		return;
	}
	Action = Animation;
	ActionStart = FMath::Clamp(StartTime, 0.f, Animation->GetPlayLength());
	ActionEnd = EndTime > ActionStart ? FMath::Min(EndTime, Animation->GetPlayLength()) : Animation->GetPlayLength();
	ActionRate = FMath::Max(0.05f, PlayRate);
	bActionStopping = false;
	++ActionSerial;
}

void UFPSRLEnemyAnimInstance::StopAction()
{
	bActionStopping = true;
}

FAnimInstanceProxy* UFPSRLEnemyAnimInstance::CreateAnimInstanceProxy()
{
	return new FFPSRLEnemyAnimProxy(this);
}

void UFPSRLEnemyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FFPSRLEnemyAnimProxy*>(InProxy);
}

void FFPSRLEnemyAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UFPSRLEnemyAnimInstance* Instance = CastChecked<UFPSRLEnemyAnimInstance>(InAnimInstance);
	Idle = Instance->Idle;
	Walk = Instance->Walk;
	Run = Instance->Run;
	WalkAnimSpeed = Instance->WalkAnimSpeed;
	RunAnimSpeed = Instance->RunAnimSpeed;
	const APawn* Pawn = Instance->TryGetPawnOwner();
	Speed = Pawn ? Pawn->GetVelocity().Size2D() : 0.f;
	Action = Instance->Action;
	ActionStart = Instance->ActionStart;
	ActionEnd = Instance->ActionEnd;
	ActionRate = Instance->ActionRate;
	ActionSerial = Instance->ActionSerial;
	bActionStopping = Instance->bActionStopping;
}

void FFPSRLEnemyAnimProxy::Update(float DeltaSeconds)
{
	IdleTime += DeltaSeconds;
	LocoSpeed = FMath::FInterpTo(LocoSpeed, Speed, DeltaSeconds, 6.f);
	// Steps match the speed: the move cycle advances by distance (walk and run share it), clamped to look sane.
	const float CycleLength = Run && LocoSpeed > WalkAnimSpeed ? Run->GetPlayLength() : (Walk ? Walk->GetPlayLength() : 1.f);
	const float AuthoredSpeed = Run && LocoSpeed > WalkAnimSpeed ? RunAnimSpeed : WalkAnimSpeed;
	const float Rate = FMath::Clamp(LocoSpeed / FMath::Max(1.f, AuthoredSpeed), 0.5f, 1.6f);
	MovePhase = FMath::Fmod(MovePhase + DeltaSeconds * Rate / FMath::Max(0.1f, CycleLength), 1.f);

	if (!Action)
	{
		ActionWeight = 0.f;
		return;
	}
	if (ActionSerial != PlayingSerial)
	{
		PlayingSerial = ActionSerial;
		ActionTime = ActionStart;	// a new action (its weight keeps blending from wherever it was)
	}
	else
	{
		ActionTime = FMath::Min(ActionTime + DeltaSeconds * ActionRate, ActionEnd);
	}
	const bool bEnding = bActionStopping || ActionEnd - ActionTime < ActionBlendOut * ActionRate;
	const float Blend = bEnding ? -DeltaSeconds / ActionBlendOut : DeltaSeconds / ActionBlendIn;
	ActionWeight = FMath::Clamp(ActionWeight + Blend, 0.f, 1.f);
}

void FFPSRLEnemyAnimProxy::Sample(const UAnimSequence* Sequence, float Time, bool bLooping, FPoseContext& Pose) const
{
	FAnimationPoseData PoseData(Pose);
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false, {}, bLooping));

	const FBoneContainer& Bones = Pose.Pose.GetBoneContainer();
	const USkeleton* SkeletonAsset = Bones.GetSkeletonAsset();
	const int32 PelvisSkeletonIndex = SkeletonAsset ? SkeletonAsset->GetReferenceSkeleton().FindBoneIndex(TEXT("pelvis")) : INDEX_NONE;
	const FCompactPoseBoneIndex Pelvis = PelvisSkeletonIndex != INDEX_NONE ? Bones.GetCompactPoseIndexFromSkeletonIndex(PelvisSkeletonIndex) : FCompactPoseBoneIndex(INDEX_NONE);
	const FCompactPoseBoneIndex Root(0);
	if (!Pelvis.IsValid() || Pose.Pose.GetParentBoneIndex(Pelvis) != Root)
	{
		return;
	}
	// Pelvis in root space = pelvis * root; then drop the root's travel on the ground (keep its height) and zero the root.
	const FTransform RootTransform = Pose.Pose[Root];
	FTransform PelvisTransform = Pose.Pose[Pelvis] * RootTransform;
	PelvisTransform.AddToTranslation(-FVector(RootTransform.GetTranslation().X, RootTransform.GetTranslation().Y, 0.f));
	Pose.Pose[Pelvis] = PelvisTransform;
	Pose.Pose[Root] = FTransform::Identity;
}

bool FFPSRLEnemyAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!Idle)
	{
		Output.ResetToRefPose();
		return true;
	}

	// Locomotion: idle -> walk (up to its authored speed) -> run.
	Sample(Idle, FMath::Fmod(IdleTime, Idle->GetPlayLength()), true, Output);
	if (Walk && LocoSpeed > 10.f)
	{
		FPoseContext Moving(this);
		const float WalkWeight = FMath::Clamp(LocoSpeed / WalkAnimSpeed, 0.f, 1.f);
		Sample(Walk, MovePhase * Walk->GetPlayLength(), true, Moving);
		if (Run && LocoSpeed > WalkAnimSpeed)
		{
			FPoseContext Running(this);
			Sample(Run, MovePhase * Run->GetPlayLength(), true, Running);
			const float RunWeight = FMath::Clamp((LocoSpeed - WalkAnimSpeed) / (RunAnimSpeed - WalkAnimSpeed), 0.f, 1.f);
			FAnimationPoseData MovingData(Moving);
			FAnimationRuntime::BlendTwoPosesTogetherInPlace(MovingData, FAnimationPoseData(Running), 1.f - RunWeight);
		}
		FAnimationPoseData OutputData(Output);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, FAnimationPoseData(Moving), 1.f - WalkWeight);
	}

	// The action over the whole body.
	if (Action && ActionWeight > 0.f)
	{
		FPoseContext Acting(this);
		Sample(Action, ActionTime, false, Acting);
		FAnimationPoseData OutputData(Output);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, FAnimationPoseData(Acting), 1.f - ActionWeight);
	}
	return true;
}
