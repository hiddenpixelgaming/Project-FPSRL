// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLCombatFeedback.generated.h"

/** A short view punch (pitch kick that springs back): melee impact. */
UCLASS()
class FPSRL_API UFPSRLPunchShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Punch")
	float Duration = 0.14f;

	/** Degrees of pitch at the peak. */
	UPROPERTY(EditAnywhere, Category = "Punch")
	float PitchAmplitude = 1.6f;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	float Elapsed = 0.f;
};

UCLASS()
class FPSRL_API UFPSRLMeleePunchShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UFPSRLMeleePunchShake(const FObjectInitializer& ObjectInitializer);
};

/**
 * Local combat feedback for the player who landed (or missed) a hit: sounds generated in code (no audio assets yet;
 * replace with real ones later) and the melee view punch. The HUD draws the hit marker.
 */
namespace FPSRLCombatFeedback
{
	FPSRL_API void PlaySound(const UObject* WorldContext, EFPSRLHitFeedback Kind);
}
