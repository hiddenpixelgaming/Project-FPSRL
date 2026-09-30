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
	double LastThinkTime = 0.0;
	bool bMoving = false;

	int32 CurrentAttack = INDEX_NONE;
	bool bExecuting = false;
	TArray<double> AttackReadyTimes;
	int32 AttacksExecuted = 0;
	int32 MoveFailures = 0;

	FTimerHandle ThinkTimer;
	FTimerHandle AttackTimer;

	TWeakObjectPtr<UFPSRLHealthComponent> Health;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle DamagedHandle;
	FDelegateHandle StunHandle;
};
