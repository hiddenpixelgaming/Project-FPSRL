// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRLEnemyScaling.generated.h"

class UAnimInstance;
class UAnimSequence;
class UAnimSequenceBase;
class UFPSRLEnemyDefinition;

/** An action's animation (an attack's wind-up, "<Attack>_Execute" for its execution, "Phase"...). StartTime..ImpactTime
 *  is the part timed to the action: played so ImpactTime lands when the wind-up (or the leap) ends; the rest plays on at
 *  that rate. Without a timing (actions that aren't timed) it plays at normal speed from StartTime. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLEnemyAttackAnim
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequenceBase> Animation;

	/** Seconds into the animation where the action starts (skips a run-up, for example). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0"))
	float StartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0"))
	float ImpactTime = 0.5f;

	/** Where it stops (blending out); 0 = the animation's end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0"))
	float EndTime = 0.f;
};

/** Locomotion for the C++ enemy animation (UFPSRLEnemyAnimInstance): idle, walk and run on the enemy's Mannequin rig,
 *  with the speed (cm/s) each was authored for so the steps match how fast it moves. */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLEnemyAnimSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "10"))
	float WalkAnimSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "10"))
	float RunAnimSpeed = 500.f;

	bool IsSet() const { return !Idle.IsNull(); }
};

/**
 * An enemy archetype's own base stats
 (every archetype has its own; there is no universal enemy health).
 * The final numbers come from the scaling stack (UFPSRLEnemyScalingSettings), applied on the server when an
 * encounter starts.
 */
UCLASS(BlueprintType)
class FPSRL_API UFPSRLEnemyDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy")
	FText DisplayName;

	/** The enemy this describes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy")
	TSoftClassPtr<APawn> EnemyClass;

	/** Health for 1 player with no modifiers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy", meta = (ClampMin = "1"))
	float BaseHealth = 100.f;

	/** Multiplier on the damage its attacks deal before scaling (its weapon supplies the raw number). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy", meta = (ClampMin = "0"))
	float BaseDamageMultiplier = 1.f;

	/** How it behaves (AI). Empty = the AI controller's default profile. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy")
	TObjectPtr<class UFPSRLEnemyBehaviorProfile> BehaviorProfile;

	/** The character players see (e.g. a Sci-Fi Trooper). It follows the enemy class's own animated skeleton (leader pose:
	 *  same bone names), which stays hidden, so the enemy keeps its animations, weapon and hit boxes. Empty = the class's
	 *  own mesh. Soft: the licensed art isn't in Git, and a missing body falls back to the class's mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	/** Replaces the enemy class's anim blueprint (e.g. the unarmed one for melee roles). Empty = the class's own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Idle / walk / run played from C++ (UFPSRLEnemyAnimInstance replaces AnimClass), with actions over the whole body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	FFPSRLEnemyAnimSet AnimSet;

	/** By action name (an attack's name for its wind-up, "<Attack>_Execute", "Phase"): the animation it plays. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TMap<FName, FFPSRLEnemyAttackAnim> AttackAnims;

	/** Size of the whole enemy (hit box included); 1 = the class's size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.5", ClampMax = "2"))
	float Scale = 1.f;

	/** Movement speed (cm/s); 0 = the class's own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy", meta = (ClampMin = "0"))
	float WalkSpeed = 0.f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("Enemy"), GetFName()); }
};

/**
 * How enemies scale with the number of players in an encounter. Health and damage are independent; index 0 = 1 player,
 * 1 = 2 players, ...; beyond the last entry the last value is used.
 */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLPlayerCountScaling
{
	GENERATED_BODY()

	/** Enemy health multiplier by player count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scaling")
	TArray<float> Health;

	/** Enemy damage multiplier by player count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scaling")
	TArray<float> Damage;

	/** Most enemies an encounter may have at once, by player count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scaling")
	TArray<int32> MaxEnemies;

	float GetHealth(int32 Players) const { return Pick(Health, Players, 1.f); }
	float GetDamage(int32 Players) const { return Pick(Damage, Players, 1.f); }
	int32 GetMaxEnemies(int32 Players) const { return MaxEnemies.IsEmpty() ? MAX_int32 : MaxEnemies[FMath::Clamp(Players, 1, MaxEnemies.Num()) - 1]; }

private:
	static float Pick(const TArray<float>& Values, int32 Players, float Default)
	{
		return Values.IsEmpty() ? Default : Values[FMath::Clamp(Players, 1, Values.Num()) - 1];
	}
};

/** One role in the mix normal combat rooms spawn (spawn points without their own enemy class). */
USTRUCT(BlueprintType)
struct FPSRL_API FFPSRLEnemyRoleMix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles")
	TSoftObjectPtr<UFPSRLEnemyDefinition> Definition;

	/** Relative chance for each spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "0"))
	float Weight = 1.f;

	/** First Depth of the run it appears in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "1"))
	int32 FirstDepth = 1;

	/** Most of this role in one encounter; 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "0"))
	int32 MaxPerEncounter = 0;

	/** Takes the highest spawn points (Marksman). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles")
	bool bHighGround = false;
};

/**
 * Enemy scaling, one layer at a time
 (Project Settings > FPSRL Enemy Scaling). Final stats, computed on the server
 * when an encounter starts (FPSRLEnemyScaling::Compute), from the players taking part at that moment:
 *
 *   FinalHealth = BaseHealth x PlayerCount x Depth x Difficulty (x infinite scaling) x Expedition x Encounter
 *   FinalDamage = BaseDamage x PlayerCount x Depth x Difficulty (x infinite scaling) x Expedition x Encounter
 *
 * Base: the enemy's UFPSRLEnemyDefinition (else its health component's default). Player count: EnemyScaling, or
 * BossScaling for a Final Level Boss encounter (or the encounter's own override). Depth: the Depth definition.
 * Difficulty and infinite scaling: here. Expedition: the run (UFPSRLRunSubsystem). Encounter: the encounter definition.
 *
 * Abyssus-verified: the player-count health curve (1.00 / 2.00 / 3.25 / 4.75), the encounter enemy caps
 * (12 / 15 / 17 / 20) and +1% health / +1% damage per infinite-scaling point. Everything else here is a project
 * balancing parameter (player-count damage, the boss profile, Depth and difficulty multipliers).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Enemy Scaling"))
class FPSRL_API UFPSRLEnemyScalingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFPSRLEnemyScalingSettings();

	static const UFPSRLEnemyScalingSettings& Get() { return *GetDefault<UFPSRLEnemyScalingSettings>(); }

	/** Enemy archetypes' base stats (looked up by the spawned enemy's class). */
	UPROPERTY(Config, EditAnywhere, Category = "Enemies")
	TArray<TSoftObjectPtr<UFPSRLEnemyDefinition>> EnemyDefinitions;

	/** The roles normal combat rooms mix (empty = the room's own enemy class everywhere). Test: fpsrl.Enemy.ForceRole. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemies")
	TArray<FFPSRLEnemyRoleMix> RoleMix;

	/** Normal enemies and Minibosses. Health and caps: Abyssus-verified. Damage: project balancing parameter. */
	UPROPERTY(Config, EditAnywhere, Category = "Player Count")
	FFPSRLPlayerCountScaling EnemyScaling;

	/** Final Level Bosses (separate: boss scaling can differ). Project balancing parameter; each boss may override. */
	UPROPERTY(Config, EditAnywhere, Category = "Player Count")
	FFPSRLPlayerCountScaling BossScaling;

	/** Global difficulty (project balancing parameters). */
	UPROPERTY(Config, EditAnywhere, Category = "Difficulty", meta = (ClampMin = "0"))
	float DifficultyHealthMultiplier = 1.f;

	UPROPERTY(Config, EditAnywhere, Category = "Difficulty", meta = (ClampMin = "0"))
	float DifficultyDamageMultiplier = 1.f;

	/** Infinite-scaling points (Deep Water style); each adds the per-point share below to health and damage. */
	UPROPERTY(Config, EditAnywhere, Category = "Difficulty", meta = (ClampMin = "0"))
	int32 InfiniteScalingPoints = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Difficulty", meta = (ClampMin = "0"))
	float InfiniteScalingHealthPerPoint = 0.01f;

	UPROPERTY(Config, EditAnywhere, Category = "Difficulty", meta = (ClampMin = "0"))
	float InfiniteScalingDamagePerPoint = 0.01f;

	/** The definition for an enemy class (nearest parent listed), or null. */
	const UFPSRLEnemyDefinition* FindDefinition(const UClass* EnemyClass) const;
};
