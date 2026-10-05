// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSRLShieldEncounterComponent.generated.h"

class AFPSRLShieldPlatform;
class UFPSRLEnemyBehaviorProfile;

UENUM(BlueprintType)
enum class EFPSRLShieldPhase : uint8
{
	Idle,				// normal combat (or waiting for the next mechanic)
	Throwing,			// its mines are flying out all round it: the pull is coming
	Charging,			// players brought in, platforms lit, the boss charges its shockwave
	AwaitingPlayers,	// the shockwave is out; not every required player is on a lit platform (disruption paused)
	Disrupting,			// every required player is placed: Shield Disruption charging
	Vulnerable			// every shield layer is broken: the mechanic is over for good
};

/**
 * The Ground Juggernaut's shields and its platform mechanic (user spec 2026-10-05). The boss's health can't be hurt while
 * any shield layer stands. Every ShieldMechanicInterval seconds while one does:
 *   1. every standing player is brought next to the boss (spread around it),
 *   2. as many of the arena's four permanent platforms as there are standing players light up (a new combination each
 *      time), and the boss charges ShieldChargeSeconds (its blast area shows on the floor),
 *   3. the epicenter shockwave goes out (AFPSRLShockwave; jumpable; players on platforms are above it),
 *   4. once EVERY standing player is on a lit platform (one each), Shield Disruption charges for ShieldDisruptionSeconds;
 *      anyone stepping off pauses it (the progress is kept),
 *   5. full: the current layer breaks and combat resumes. After the last layer: vulnerable, the mechanic stops.
 * Downed or dead players stop counting (a lit platform nobody can fill goes dark). The boss's attacks pause from (1) to
 * (5). Values from its behaviour profile (Shield Encounter).
 *
 * Server-driven by timers (no Tick); state replicated for the HUD (UFPSRLEncounterBarWidget: SHIELD 1 | SHIELD 2 | HP
 * and the SHIELD DISRUPTION bar) and the boss's shield glow. Added to the boss by UFPSRLEnemyBodySubsystem from its
 * definition (EncounterComponent). Test: fpsrl.Juggernaut.MechanicInterval overrides the interval.
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLShieldEncounterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLShieldEncounterComponent();

	int32 GetShieldsRemaining() const { return ShieldsRemaining; }
	int32 GetShieldLayers() const { return ShieldLayers; }
	EFPSRLShieldPhase GetPhase() const { return Phase; }
	/** Shield Disruption 0..1. */
	float GetDisruptionFraction() const { return DisruptionSeconds > 0.f ? FMath::Clamp(DisruptionProgress / DisruptionSeconds, 0.f, 1.f) : 0.f; }
	float GetDisruptionSecondsLeft() const { return FMath::Max(0.f, DisruptionSeconds - DisruptionProgress); }
	int32 GetRequiredCount() const { return RequiredCount; }
	/** The pull under way is a forced one (nobody was in sight): no platforms. */
	bool IsForcedPull() const { return bForcedPullShown; }

	/** Any machine: the shields / phase / disruption changed (the HUD). */
	FSimpleMulticastDelegate OnStateChanged;

	/** Server (tests): the lit platforms, and start the mechanic now. */
	TArray<AFPSRLShieldPlatform*> GetHighlightedPlatforms() const;
	void StartMechanicNow();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	const UFPSRLEnemyBehaviorProfile* GetProfile() const;
	void Watch();
	void ScheduleMechanic();
	void StartMechanic();
	/** A pull: mines thrown, then players pulled in; forced (nobody in sight) = a short charge and no platforms. */
	void StartPull(bool bForced);
	void PullAndCharge();
	void CheckSight();
	void ReleaseShockwave();
	void CheckPlayers();
	void BreakShield();
	void EndMechanic(bool bResumeLater);
	void SetPhase(EFPSRLShieldPhase NewPhase);
	void SetAttacksPaused(bool bPaused);
	void UpdateRequired();
	TArray<APawn*> GetStandingPlayers() const;

	UFUNCTION()
	void HandleDeath(AController* Killer, AActor* Causer);

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 ShieldsRemaining = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 ShieldLayers = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EFPSRLShieldPhase Phase = EFPSRLShieldPhase::Idle;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	float DisruptionProgress = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	float DisruptionSeconds = 5.f;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 RequiredCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bForcedPullShown = false;

	bool bActive = false;
	bool bForcedPull = false;
	double BlindSince = 0.0;
	double NextForcedPull = 0.0;
	FTimerHandle BlindTimer;
	bool bDead = false;
	double LastCheckTime = 0.0;
	uint32 LastSelection = 0;
	TArray<TWeakObjectPtr<APawn>> Required;
	TArray<TWeakObjectPtr<AFPSRLShieldPlatform>> Highlighted;
	FTimerHandle WatchTimer;
	FTimerHandle MechanicTimer;
	FTimerHandle ChargeTimer;
	FTimerHandle CheckTimer;
	int32 LastShieldsShown = -1;

	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> Dome;
};
