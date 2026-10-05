// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Data/FPSRLEnemyBehaviorProfile.h"
#include "GameplayTagContainer.h"
#include "FPSRLEnemyAIController.generated.h"

class AFPSRLWeapon;
class UAbilitySystemComponent;
class UFPSRLHealthComponent;
struct FRandomStream;

namespace FPSRLEnemyAI
{
	/** A player an enemy could target, as target selection sees it. */
	struct FPSRL_API FTargetCandidate
	{
		AActor* Actor = nullptr;
		float Distance = 0.f;
		float HealthFraction = 1.f;
		float Threat = 0.f;
		bool bLastDamager = false;
		bool bCurrent = false;
	};

	/** Index of the candidate the strategy picks (INDEX_NONE if none). Pure: the same for every enemy archetype. */
	FPSRL_API int32 SelectTarget(EFPSRLTargetSelection Strategy, const TArray<FTargetCandidate>& Candidates, float PreferredDistance, FRandomStream* Random = nullptr);

	FPSRL_API const TCHAR* StateName(EFPSRLEnemyAIState State);
}

/**
 * The enemy AI foundation (server only). One controller class for every enemy; the archetype's behaviour comes from
 * its UFPSRLEnemyBehaviorProfile (via its UFPSRLEnemyDefinition, else DefaultProfile).
 *
 *  - Starts Inactive: nothing happens until its encounter starts (AFPSRLRoom::StartCombat -> SetEncounterActive).
 *  - Decisions on a timer (profile ThinkInterval), not Tick; attacks run windup -> execute -> recovery on timers.
 *  - Detection: range, cone, line of sight, proximity, damage aggro, memory after losing sight.
 *  - Targeting: every active player is a candidate; each enemy picks by its own strategy (FPSRLEnemyAI::SelectTarget);
 *    targets are dropped when they leave, die / go down (unless allowed), go past the leash or are destroyed.
 *  - Movement: Unreal navigation (crowd following for avoidance); the profile's band and style decide where to go.
 *  - Attacks: the profile's list, first valid by priority (range, angle, sight, cooldown); FireWeapon uses the enemy's
 *    own weapon (which asks this controller where to aim), Melee the shared melee sweep, GameplayAbility its GAS abilities.
 *  - Reactions: damage response, Status.Stunned = Disabled, death = Dead.
 */
UCLASS()
class FPSRL_API AFPSRLEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AFPSRLEnemyAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Profile used when the enemy's definition has none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UFPSRLEnemyBehaviorProfile> DefaultProfile;

	/** Server: its encounter started (true) or was reset (false: back to Inactive). */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetEncounterActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "AI")
	EFPSRLEnemyAIState GetAIState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "AI")
	AActor* GetTarget() const { return Target.Get(); }

	const UFPSRLEnemyBehaviorProfile* GetProfile() const { return Profile; }

	/** Replace the profile (tests, special encounters). */
	void SetProfile(UFPSRLEnemyBehaviorProfile* NewProfile);

	/** Where its weapon aims (the weapon asks while an enemy holds it). */
	FVector GetAimPoint() const;

	/** The weapon's semi-auto refire: keep shooting while a FireWeapon attack executes. */
	bool WantsToKeepFiring() const;

	/** A clear line from its eyes to the actor (walls block; pawns don't). */
	bool HasLineOfSightTo(const AActor* Other) const;

	/** No wall between the two points (bodies and carried weapons ignored). */
	bool IsLineClear(const FVector& From, const FVector& To, const AActor* Other) const;

	/** Attacks it has executed (tests / debug). */
	int32 GetAttacksExecuted() const { return AttacksExecuted; }
	int32 GetMoveFailures() const { return MoveFailures; }
	int32 GetLeapsLanded() const { return LeapsLanded; }
	int32 GetLeapsCancelled() const { return LeapsCancelled; }
	int32 GetProjectilesFired() const { return ProjectilesFired; }
	int32 GetTimesStuck() const { return TimesStuck; }

	/** An encounter mechanic pauses its attacks (the Juggernaut's platforms): none start; one in progress is cancelled. */
	void SetAttacksPaused(bool bPaused);
	bool AreAttacksPaused() const { return bAttacksPaused; }
	int32 GetPhases() const { return Phases; }
	int32 GetMeleeHits() const { return MeleeHits; }

	/** Server: groups the squad-moving enemies among these (profile bSquadMovement) into squads of nearby members (called
	 *  when an encounter starts). The first member of each squad leads; when it dies the next one does. */
	static void FormSquads(const TArray<AFPSRLEnemyAIController*>& Controllers);

	/** Its squad's leader (itself when it leads or has no squad). */
	AFPSRLEnemyAIController* GetSquadLeader() const;
	int32 GetSquadSize() const;
	FString Describe() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	// Decisions
	void Think();
	void SetState(EFPSRLEnemyAIState NewState);
	void AcquireTarget(AActor* NewTarget, bool bReact);
	void ClearTarget();
	bool IsValidTarget(const AActor* Candidate) const;
	bool CanDetect(const AActor* Candidate) const;
	TArray<FPSRLEnemyAI::FTargetCandidate> GatherCandidates(bool bDetectableOnly) const;
	AActor* ChooseTarget(bool bDetectableOnly) const;
	void UpdateMovement(float Distance);
	void HandleLostSight(float Distance);

	// Attacks
	int32 SelectAttack(float Distance, bool bSeesTarget) const;
	void BeginAttack(int32 AttackIndex);
	void ExecuteAttack();
	void EndAttackExecution();
	void FinishRecovery();
	void CancelAttack(float RecoverySeconds);

	// Movement helpers
	bool MoveToSpot(const FVector& Spot, EFPSRLEnemyAIState MoveState);
	bool FindSpotAroundTarget(float MinDistance, float MaxDistance, bool bNeedSight, FVector& OutSpot) const;
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	bool IsCrowded(const FVector& Spot) const;
	FVector GetEyeLocation() const;

	// Reactions
	void HandleDamaged(float Damage, APawn* Attacker);

	/** Profile bPhaseOnProjectileHit (Skirmisher): shot -> leaps back away from the shooter, phased for PhaseSeconds. */
	void HandleProjectileHit(float Damage, APawn* Attacker);

	/** LeapSlam: launched on an arc to where its target stands; landing (or a safety timer) sends the shockwave and ends
	 *  the execution (its recovery is the pause after the leap). */
	void StartLeap(const FFPSRLEnemyAttack& Attack);
	/** The leap's arc to the target (its launch velocity), or false when level geometry is in the way. */
	bool PlanLeap(const FFPSRLEnemyAttack& Attack, FVector* OutVelocity) const;

	/** GroundStrike: a mark under each target that explodes after the warning. */
	void ExecuteGroundStrike(const FFPSRLEnemyAttack& Attack);
	/** DeployMines: landmines on free floor around it, up to its cap. */
	void DeployMines(const FFPSRLEnemyAttack& Attack);
	/** VolleyHeavy: the next step of the volley (a slow shot, the aim line, or the heavy shot). */
	void FireVolleyShot();
	/** Stuck twice in the same place: walk to a reachable spot 2-5 m away (not towards BlockedToward) before chasing again.
	 *  bStranded (its path is partial: ground the navmesh doesn't join to the target's): walk straight at the target instead. */
	void StepAside(const FVector& BlockedToward, bool bStranded);
	void HopAtTarget();
	void FireProjectileAtTarget(const FFPSRLEnemyAttack& Attack, float Damage, float SpeedMultiplier);
	int32 VolleyShotsLeft = 0;
	bool bHeavyAiming = false;
	UFUNCTION()
	void HandleLeapLanded(const FHitResult& Hit);
	void FinishLeap();

	/** Leaps (LeapSlam, the phase leap) fly the arc they were launched on: no air braking or friction until it lands. */
	void BeginAirborne(ACharacter* EnemyCharacter);
	void EndAirborne();
	float SavedBrakingFalling = -1.f;
	float SavedFallingFriction = 0.f;

	/** The nearest valid player (bTargetNearestAfter). */
	AActor* FindNearestPlayer() const;
	void HandleStunTagChanged(const FGameplayTag Tag, int32 NewCount);
	UFUNCTION()
	void HandleDeath(AController* KillerInstigator, AActor* Causer);

	AFPSRLWeapon* FindWeapon() const;
	UFPSRLEnemyBehaviorProfile* ResolveProfile(const APawn* InPawn) const;

	UPROPERTY(Transient)
	TObjectPtr<UFPSRLEnemyBehaviorProfile> Profile;

	EFPSRLEnemyAIState State = EFPSRLEnemyAIState::Inactive;
	EFPSRLEnemyAIState StateBeforeDisabled = EFPSRLEnemyAIState::Idle;
	bool bEncounterActive = false;

	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> LastDamager;
	TMap<TWeakObjectPtr<AActor>, float> Threat;
	FVector LastKnownTargetLocation = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;
	FVector MoveGoal = FVector::ZeroVector;
	double LastSeenTime = -1.0e9;
	double ReactionEndTime = 0.0;
	double NextRetargetTime = 0.0;
	double NextRepositionTime = 0.0;
	double SearchEndTime = 0.0;
	double IdleSince = 0.0;
	double LastThinkTime = 0.0;
	bool bMoving = false;

	int32 CurrentAttack = INDEX_NONE;
	bool bExecuting = false;
	TArray<double> AttackReadyTimes;
	int32 AttacksExecuted = 0;
	int32 MoveFailures = 0;

	FTimerHandle ThinkTimer;
	FTimerHandle AttackTimer;

	/** Squad (Grunts): members share one list; each holds a spot around the first living member. */
	struct FSquad
	{
		TArray<TWeakObjectPtr<AFPSRLEnemyAIController>> Members;
	};
	TSharedPtr<FSquad> Squad;
	FVector LastSquadGoal = FVector::ZeroVector;
	bool IsAliveForSquad() const;
	/** A follower's movement: walk back to its spot by the leader when too far, else stay. False when it leads / has
	 *  no squad (its own movement applies). */
	bool FollowSquad();

	TWeakObjectPtr<UFPSRLHealthComponent> Health;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle DamagedHandle;
	FDelegateHandle ProjectileHitHandle;
	FTimerHandle LeapSafetyTimer;
	FTimerHandle PhaseTimer;
	bool bLeaping = false;
	double NextPhaseTime = 0.0;
	int32 LeapsLanded = 0;
	int32 LeapsCancelled = 0;
	bool bAttacksPaused = false;
	int32 ProjectilesFired = 0;
	double StuckSince = -1.0;
	int32 TimesStuck = 0;
	FVector LastStuckLocation = FVector::ZeroVector;
	double LastStuckTime = -100.0;
	double SteppingAsideUntil = 0.0;
	int32 StuckRepeats = 0;
	/** Where its leap lands, while in the air (others leap somewhere else). */
	FVector LeapGoal = FVector::ZeroVector;
	int32 Phases = 0;
	int32 MeleeHits = 0;
	FDelegateHandle StunHandle;
};
