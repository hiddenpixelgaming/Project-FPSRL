// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "FPSRLHealthComponent.generated.h"

class UAbilitySystemComponent;
class UFPSRLHealthSet;
class UDamageType;
struct FOnAttributeChangeData;

// Parameter types deliberately match the old BP_HealthComponent dispatchers (Blueprint "Float" = double),
// so existing Blueprint handlers bound to these keep a matching signature.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSRLHealthChangedEvent, double, CurrentHealth, double, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSRLDeathEvent, AController*, Instigator, AActor*, Causer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFPSRLDownedEvent, bool, bIsDowned);
/** Server: damage landed (after scaling), and the pawn that dealt it (null for environment damage). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FFPSRLDamagedByEvent, float /*Damage*/, APawn* /*Attacker*/);

/**
 * Health for any damageable pawn, backed by GAS. BP_HealthComponent derives from this.
 *
 * The values live in a UFPSRLHealthSet on an Ability System Component; this component is the friendly
 * Blueprint-facing view of them (events, IsDead, percent) plus the damage bridge from the engine's AnyDamage event.
 *
 *  - Players: the ASC and HealthSet live on AFPSRLPlayerState, which calls InitializeWithAbilitySystem when the
 *    pawn is possessed (server) or its PlayerState replicates (client).
 *  - Enemies: the pawn has its own UFPSRLAbilitySystemComponent; this component finds it at BeginPlay and the
 *    server adds a HealthSet to it.
 *
 * Replication: none of its own. Health replicates through the HealthSet; OnHealthChanged fires on every machine
 * when the value arrives. OnDeath fires on the server only (it is a gameplay decision).
 */
UCLASS(ClassGroup = (FPSRL), meta = (BlueprintSpawnableComponent))
class FPSRL_API UFPSRLHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** An enemy wearing a role body (UFPSRLEnemyBodySubsystem): never the testing tint, it keeps its own colours. */
	void SetKeepsOwnColours();

	/** Phased (Skirmisher): projectiles pass through it and do nothing; melee still hurts. Every machine (clients fly
	 *  their own projectile copies); set by UFPSRLEnemyRoleComponent. */
	void SetPhased(bool bInPhased);
	bool IsPhased() const { return bPhased; }

	/** Server: shielded (a boss's shield layers): no damage at all until it's cleared. */
	void SetInvulnerable(bool bInInvulnerable) { bInvulnerable = bInInvulnerable; }
	bool IsInvulnerable() const { return bInvulnerable; }

	UFPSRLHealthComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Health and MaxHealth are set to this on the server when initialized (each spawn/respawn). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1"))
	float DefaultMaxHealth = 100.f;

	/** Fires on server and clients whenever Health or MaxHealth changes. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FFPSRLHealthChangedEvent OnHealthChanged;

	/** Fires once on the server when Health reaches 0. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FFPSRLDeathEvent OnDeath;

	/** Server: after each hit lands (AI reactions, threat). */
	FFPSRLDamagedByEvent OnDamagedBy;

	/** Server: a projectile hit landed (after OnDamagedBy), and the pawn that fired it. */
	FFPSRLDamagedByEvent OnProjectileHit;

	/** Player side (a human player's controller or pawn) vs enemy side. */
	static bool IsPlayerSide(const AController* Controller, const AActor* Actor);

	/** Bridge from the engine damage path: forward an actor's Event AnyDamage here. Server-only; ignored on clients. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void HandleTakeAnyDamage(float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	/** Server-only; ignored on clients. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float Amount);

	// Kept as exec (non-pure) nodes to match the Blueprint functions they replace, so existing graphs stay wired.
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Health")
	bool IsDead() const;

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const;

	/** How many of these actors have a health component and are still alive (actors without one are not counted). */
	UFUNCTION(BlueprintPure, Category = "Health")
	static int32 CountAliveActors(const TArray<AActor*>& Actors);

	/** Bind to an ASC's HealthSet. The server also creates the set if missing and resets Health to DefaultMaxHealth. */
	void InitializeWithAbilitySystem(UAbilitySystemComponent* InASC);
	void UninitializeFromAbilitySystem();

	// --- Downed (players in a run) ----------------------------------------------------------------------------
	// Lethal damage on a Status.Downable player downs them instead (health held at 1, damage ignored, crawling at
	// reduced speed, can't leave ledges) while a teammate is still up to revive them. Otherwise they die. If every
	// player ends up down, all of them die (party wipe). State = the replicated Status.Downed tag on the ASC.

	/** Fires on server and clients when this pawn goes down or gets back up. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FFPSRLDownedEvent OnDownedChanged;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDowned() const;

	/** Playtesting god mode is on for this player (their PlayerState's bGodMode): no damage, no death. */
	bool IsGodMode() const;

	/** Server: a teammate revived this player. Back up with Fraction of max health. */
	void Revive(float HealthFraction);

	/** Server: finish a downed (or living) player off: health 0, OnDeath. */
	void Kill();

	/** Server: multiply max health (and refill to it), e.g. a Miniboss / Final Level Boss encounter. Works before or after BeginPlay. */
	void ScaleMaxHealth(float Multiplier);

	/** Server: max health (and current health) = NewMax, from enemy scaling. DefaultMaxHealth stays the unscaled base. */
	void SetMaxHealthServer(float NewMax);

	/** Server: this enemy's attacks deal this much of their damage (enemy scaling). 1 for players. */
	float OutgoingDamageMultiplier = 1.f;

	/** Server: damage from the world itself (a fall), not from anyone, so the friendly-fire filter doesn't apply. */
	void ApplyEnvironmentDamage(float Amount);

	/** Alive and not downed: can fight, revive teammates, vote at the portal. */
	static bool IsPawnUp(const APawn* Pawn);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	const UFPSRLHealthSet* GetHealthSet() const;
	void HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser);
	void HandleDowned(AActor* DamageInstigator, AActor* DamageCauser);
	void HandleDownedTagChanged(const struct FGameplayTag Tag, int32 NewCount);
	void ApplyDownedMovement(bool bDowned);

	/** Dead or downed: the body lets projectiles through (shots reach a reviver); alive again: it blocks. */
	void UpdateProjectileBlocking();

	/** Each body part's projectile response before it was set to ignore (restored on revive). */
	TArray<TPair<TWeakObjectPtr<UPrimitiveComponent>, ECollisionResponse>> SavedProjectileResponses;

	/** Object channel projectiles fly on (the project's "Projectile" channel). Dead bodies ignore it. */
	UPROPERTY(EditDefaultsOnly, Category = "Health")
	TEnumAsByte<ECollisionChannel> ProjectileChannel = ECC_GameTraceChannel1;

	bool bBodyIgnoresProjectiles = false;
	bool bPhased = false;
	bool bInvulnerable = false;

	/** Testing aid (console: fpsrl.TintEnemies 0 to turn off): enemies' materials are tinted this colour on every machine. */
	void ApplyEnemyTestTint();
	bool bKeepsOwnColours = false;

	/** Every body-colour parameter ('Tint' / 'Color') on this character's skeletal mesh materials set to Color; the
	 *  first time a material is touched its own values are remembered so RestoreBodyColor can put them back. */
	void SetBodyColor(const FLinearColor& Color);

	/** Back to the resting look: the enemy test tint for enemies, the materials' own colours otherwise. */
	void RestoreBodyColor();

	/** Enemy hit: red, then white, then back (about 0.1 s). Every machine (health replicates). */
	void StartHitFlash();
	void StepHitFlash();

	TMap<TWeakObjectPtr<class UMaterialInstanceDynamic>, TArray<TPair<FName, FLinearColor>>> OriginalBodyColors;
	FTimerHandle HitFlashTimer;
	int32 HitFlashStage = 0;
	float LastHealthSeen = -1.f;
	bool bRestingTintIsEnemyTint = false;

	UPROPERTY(EditDefaultsOnly, Category = "Health|Testing")
	FLinearColor EnemyTestTintColor = FLinearColor(1.f, 0.85f, 0.f);

	/** Server: downed players are ignored by enemy AI (tag removed, perception forgotten); revived ones are fair game again. */
	void SetTargetableByAI(bool bTargetable);

	/** Actor tag the enemy AI senses as a target (the shooter template's "Player"). Removed while downed. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|Downed")
	FName AITargetTag = TEXT("Player");

	bool bRemovedAITargetTag = false;

	/** Damage filter: no friendly fire (player vs player, enemy vs enemy, self) and nothing from downed attackers. */
	bool ShouldAcceptDamageFrom(const AController* InstigatedBy, const AActor* DamageCauser) const;

	bool IsAnyOtherPlayerUp() const;
	void ApplyHealthEffect(TSubclassOf<class UGameplayEffect> EffectClass, const struct FGameplayTag& MagnitudeTag, float Magnitude,
		AController* InstigatedBy, AActor* Causer);

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/** Server: OnDeath has fired. Kept after the ASC is detached (body removed), so IsDead stays true. */
	bool bDied = false;

	/** Server: the "Press E to Revive" station following this downed pawn. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> ReviveMarker;

	FDelegateHandle DownedTagHandle;

	// Movement values replaced while downed (restored on revive).
	bool bDownedMovementApplied = false;
	float SavedMaxWalkSpeed = 0.f;
	float SavedJumpZVelocity = 0.f;
	bool bSavedCanWalkOffLedges = true;
	bool bSavedCanWalkOffLedgesWhenCrouching = true;

	// --- Downed camera (owning player only) --------------------------------------------------------------------
	// While down the owner watches their body from a third-person camera behind it; revived, back to first person.

	/** Local player's own pawn: swap first-person camera and meshes for a third-person view of the body, or back. */
	void ApplyDownedCamera(bool bDowned);

	/** Third-person camera distance behind the downed body. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|Downed")
	float DownedCameraDistance = 350.f;

	/** Third-person camera offset (camera space: X back/forward, Y side, Z up) from the end of the arm. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|Downed")
	FVector DownedCameraOffset = FVector(0.f, 0.f, 80.f);

	UPROPERTY(Transient)
	TObjectPtr<class USpringArmComponent> DownedSpringArm;

	UPROPERTY(Transient)
	TObjectPtr<class UCameraComponent> DownedCamera;

	/** What ApplyDownedCamera changed, to put back on revive. */
	struct FDownedViewChange
	{
		TWeakObjectPtr<class UPrimitiveComponent> Primitive;
		EFirstPersonPrimitiveType FirstPersonType = EFirstPersonPrimitiveType::None;
		bool bOwnerNoSee = false;
		bool bHiddenInGame = false;
	};
	TArray<FDownedViewChange> DownedViewChanges;
	TArray<TWeakObjectPtr<class UCameraComponent>> DownedDeactivatedCameras;
	bool bDownedCameraApplied = false;
};
