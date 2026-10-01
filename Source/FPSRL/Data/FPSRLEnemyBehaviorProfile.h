// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FPSRLEnemyBehaviorProfile.generated.h"

/**
 * Enemy AI states (AFPSRLEnemyAIController). An archetype only passes through the ones its profile uses.
 *   Inactive -> (encounter starts) -> Idle -> Detecting -> Targeting -> Pursuing / Positioning -> Attacking -> Recovering
 *   -> Repositioning -> Pursuing / Attacking ...; Searching after losing sight; Disabled while stunned; Dead.
 */
UENUM(BlueprintType)
enum class EFPSRLEnemyAIState : uint8
{
	Inactive,		// spawned, its encounter hasn't started: no targeting, movement or attacks
	Idle,			// active, no target
	Detecting,		// just noticed a player (reaction time)
	Targeting,		// choosing / switching target
	Pursuing,		// too far: approaching
	Positioning,	// in its preferred band: holding / strafing
	Attacking,		// windup + execution of one attack
	Recovering,		// after an attack (or an interruption)
	Repositioning,	// moving to a better spot (too close, lost sight, spacing)
	Searching,		// lost its target: checking the last known position
	Disabled,		// stunned (Status.Stunned on its ability system)
	Dead
};

/** How an enemy picks among the valid players (each enemy decides on its own, on the server). */
UENUM(BlueprintType)
enum class EFPSRLTargetSelection : uint8
{
	ClosestPlayer,
	LastDamagingPlayer,			// whoever hurt it last (else closest)
	HighestThreat,				// most damage dealt to it recently (else closest)
	LowestHealthPlayer,
	RandomValidPlayer,
	PreferredRangePlayer,		// the one nearest to its preferred distance
	CurrentTargetUntilInvalid	// keeps its first target until it becomes invalid (then closest)
};

/** What it does in its preferred distance band. */
UENUM(BlueprintType)
enum class EFPSRLMovementStyle : uint8
{
	Chase,				// keep closing in to its attack range
	MaintainDistance,	// hold position while in the band
	Strafe,				// side-step around the target every so often
	Reposition,			// move to a new spot in the band (with sight of the target) every so often
	Retreat,			// keep backing off toward the far edge of the band
	GuardPosition		// stay near where it started; never follows beyond its guard radius
};

/** When the target is closer than PreferredMinDistance. */
UENUM(BlueprintType)
enum class EFPSRLTooCloseResponse : uint8
{
	HoldAndAttack,	// stays and keeps attacking
	Retreat,		// backs away to the band
	Reposition		// moves to another spot in the band
};

/** When it can't see its target any more. */
UENUM(BlueprintType)
enum class EFPSRLLostSightResponse : uint8
{
	Reposition,		// find a spot with sight of the target
	Pursue,			// go toward the target
	Search,			// go to the last known position and look around, then give up
	Retarget		// pick another (visible) player
};

/** When it takes damage. */
UENUM(BlueprintType)
enum class EFPSRLDamageResponse : uint8
{
	KeepAttacking,		// nothing changes (still gains threat / aggro)
	RetargetAttacker,	// switches to whoever hit it
	Interrupt,			// an interruptible attack in progress is cancelled (short recovery)
	ContinuePursuit,	// if not attacking, heads for its target
	Reposition			// moves to a new spot
};

/** What an attack does when it executes. */
UENUM(BlueprintType)
enum class EFPSRLEnemyAttackAction : uint8
{
	FireWeapon,			// holds its weapon's trigger for ExecuteSeconds (the weapon's own fire rate, projectiles, damage)
	Melee,				// one server sweep in front of it (the players' melee rules)
	GameplayAbility		// activates the abilities with AbilityTag on its ability system
};

/** One attack an archetype can choose (evaluated in Priority order; the first valid one is used). */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLEnemyAttack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	FName Name = TEXT("Attack");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	EFPSRLEnemyAttackAction Action = EFPSRLEnemyAttackAction::FireWeapon;

	/** Higher is tried first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	int32 Priority = 0;

	/** Conditions: target distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Conditions", meta = (ClampMin = "0"))
	float MinRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Conditions", meta = (ClampMin = "0"))
	float MaxRange = 1500.f;

	/** Conditions: the target within this many degrees of its facing (180 = anywhere). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Conditions", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxAngle = 45.f;

	/** Conditions: needs a clear line to the target (no shooting through walls). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Conditions")
	bool bRequiresLineOfSight = true;

	/** Seconds after the attack ends before it can be used again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Timing", meta = (ClampMin = "0"))
	float Cooldown = 1.f;

	/** Telegraph before it executes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Timing", meta = (ClampMin = "0"))
	float WindupSeconds = 0.2f;

	/** How long it executes (FireWeapon: trigger held; others: active time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Timing", meta = (ClampMin = "0"))
	float ExecuteSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Timing", meta = (ClampMin = "0"))
	float RecoverySeconds = 0.3f;

	/** Stops moving while attacking (false: keeps its movement going, e.g. strafe-shooting). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	bool bHoldPosition = true;

	/** A damage response of Interrupt can cancel it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	bool bInterruptible = false;

	/** Melee: damage, sweep radius and targets per swing (reach = MaxRange). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Melee", meta = (ClampMin = "0"))
	float MeleeDamage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Melee", meta = (ClampMin = "1"))
	float MeleeRadius = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Melee", meta = (ClampMin = "1"))
	int32 MeleeMaxTargets = 1;

	/** GameplayAbility: abilities with this tag on its ability system. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Ability")
	FGameplayTag AbilityTag;
};

/**
 * How one enemy archetype behaves (referenced by its UFPSRLEnemyDefinition). The controller supplies the capabilities
 * (detection, targeting, movement, attacks, reactions); the profile picks which ones and how.
 * Every number here is a project balancing parameter unless its comment says otherwise.
 */
UCLASS(BlueprintType)
class FPSRL_API UFPSRLEnemyBehaviorProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Detection -----------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0"))
	float DetectionRange = 2500.f;

	/** Full cone angle it sees in (360 = all around). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0", ClampMax = "360"))
	float DetectionAngle = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	bool bRequireLineOfSightToDetect = true;

	/** Being hurt makes it notice the attacker (wherever they are). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	bool bAggroOnDamage = true;

	/** Players this close are noticed regardless of angle and sight (0 = off). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0"))
	float ProximityAggroRadius = 500.f;

	/** Seconds it keeps its target after losing sight (0 = drops it at once). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0"))
	float TargetMemorySeconds = 4.f;

	/** When its encounter starts it already knows the players are there (picks a target without seeing one, then moves to
	 *  get sight per LostSightResponse). Off: it has to detect them first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection")
	bool bAlertedOnEncounterStart = true;

	/** Alerted enemies left without a target (lost behind cover, memory ran out) take the nearest player again after
	 *  this many seconds instead of idling for the rest of the fight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0", EditCondition = "bAlertedOnEncounterStart"))
	float IdleRehuntSeconds = 2.f;

	/** Delay between noticing and acting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0"))
	float ReactionTime = 0.3f;

	// --- Aim (fairness: shots a player can avoid) ------------------------------------------------------------------------

	/** Each shot goes off its aim by up to this many degrees (a cone around the line to the aim point). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0", ClampMax = "30"))
	float AimSpreadDegrees = 0.f;

	/** It aims where its target was this long ago (from the target's velocity), so a player who keeps moving sideways
	 *  makes it miss; one who stands still gets hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0", ClampMax = "2"))
	float AimLagSeconds = 0.f;

	/** Its projectiles fly at this share of their normal speed (slower = visible and dodgeable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ProjectileSpeedMultiplier = 1.f;

	// --- Targeting -----------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
	EFPSRLTargetSelection TargetSelection = EFPSRLTargetSelection::ClosestPlayer;

	/** Seconds between re-evaluating the strategy (not for CurrentTargetUntilInvalid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.1"))
	float RetargetInterval = 3.f;

	/** Downed players stay valid targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
	bool bTargetDownedPlayers = false;

	/** A target farther than this is dropped (leash). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0"))
	float MaxTargetDistance = 6000.f;

	/** HighestThreat: threat (damage taken from a player) lost per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0"))
	float ThreatDecayPerSecond = 10.f;

	// --- Distance and movement -----------------------------------------------------------------------------------------

	/** Its preferred distance band to the target: farther = approach, closer = TooCloseResponse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float PreferredMinDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float PreferredMaxDistance = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EFPSRLMovementStyle MovementStyle = EFPSRLMovementStyle::MaintainDistance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EFPSRLTooCloseResponse TooCloseResponse = EFPSRLTooCloseResponse::Retreat;

	/** Strafe / Reposition: seconds between moves while in the band, and how far a step goes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.2"))
	float RepositionInterval = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "50"))
	float StrafeDistance = 400.f;

	/** GuardPosition: how far from its start it may go. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float GuardRadius = 800.f;

	/** Spots closer than this to another enemy are rejected when it picks where to go (spacing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float SeparationRadius = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "10"))
	float AcceptanceRadius = 60.f;

	// --- Reactions -----------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions")
	EFPSRLLostSightResponse LostSightResponse = EFPSRLLostSightResponse::Reposition;

	/** Search: seconds looking around the last known position before giving up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions", meta = (ClampMin = "0"))
	float SearchDuration = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions")
	EFPSRLDamageResponse DamageResponse = EFPSRLDamageResponse::KeepAttacking;

	/** Recovery after an interrupted attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reactions", meta = (ClampMin = "0"))
	float InterruptRecoverySeconds = 0.4f;

	// --- Attacks -------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attacks")
	TArray<FFPSRLEnemyAttack> Attacks;

	/** Seconds between the controller's decisions (a timer, not Tick). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (ClampMin = "0.05"))
	float ThinkInterval = 0.2f;

	/** Design note: the reference behaviour this archetype reproduces (Abyssus enemy, verified or not). */
	UPROPERTY(EditAnywhere, Category = "AI", meta = (MultiLine = "true"))
	FString ReferenceBehaviour;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("EnemyBehavior"), GetFName()); }
};
