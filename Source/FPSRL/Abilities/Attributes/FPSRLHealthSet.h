// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "FPSRLHealthSet.generated.h"

/** Fired on the server once, when Health first reaches 0. Instigator/Causer come from the killing effect's context. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FFPSRLOutOfHealthEvent, AActor* /*Instigator*/, AActor* /*Causer*/);

/**
 * Health attributes for anything that can be damaged (players, enemies, boss).
 *
 * Health and MaxHealth replicate. Damage and Healing are "meta" attributes: server-only scratch values that a
 * Gameplay Effect writes into, which this set then converts into a Health change and resets to 0. Routing damage
 * through a meta attribute lets buffs, resistances and invulnerability all modify or veto it in one place.
 *
 * Never set Health directly from gameplay code; apply UFPSRLDamageEffect / UFPSRLHealEffect so the change
 * goes through GAS aggregation and prediction.
 */
UCLASS()
class FPSRL_API UFPSRLHealthSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRLHealthSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLHealthSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLHealthSet, MaxHealth)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLHealthSet, Damage)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLHealthSet, Healing)

	/** Server-only. See FFPSRLOutOfHealthEvent. */
	mutable FFPSRLOutOfHealthEvent OnOutOfHealth;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

protected:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Health", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData Health;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Health", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MaxHealth;

	/** Meta attribute: incoming damage, converted to -Health in PostGameplayEffectExecute. Not replicated. */
	UPROPERTY(BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData Damage;

	/** Meta attribute: incoming healing, converted to +Health in PostGameplayEffectExecute. Not replicated. */
	UPROPERTY(BlueprintReadOnly, Category = "Health", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData Healing;

	/** Guards OnOutOfHealth so it fires once per death; reset when Health goes back above 0 (respawn, revive). */
	bool bOutOfHealth = false;
};
