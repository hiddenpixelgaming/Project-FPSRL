// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSRLHealthComponent.generated.h"

class UAbilitySystemComponent;
class UFPSRLHealthSet;
class UDamageType;
struct FOnAttributeChangeData;

// Parameter types deliberately match the old BP_HealthComponent dispatchers (Blueprint "Float" = double),
// so existing Blueprint handlers bound to these keep a matching signature.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSRLHealthChangedEvent, double, CurrentHealth, double, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSRLDeathEvent, AController*, Instigator, AActor*, Causer);

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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	const UFPSRLHealthSet* GetHealthSet() const;
	void HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser);
	void ApplyHealthEffect(TSubclassOf<class UGameplayEffect> EffectClass, const struct FGameplayTag& MagnitudeTag, float Magnitude,
		AController* InstigatedBy, AActor* Causer);

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;
};
