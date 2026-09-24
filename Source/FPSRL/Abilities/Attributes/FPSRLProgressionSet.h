// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "FPSRLProgressionSet.generated.h"

/**
 * Player progression capacities. MaxBoonSlots starts at 20; the Talent Tree raises it with a permanent Gameplay
 * Effect (tagged Effect.Permanent.Talent) modifying this attribute - so the cap is never hardcoded.
 */
UCLASS()
class FPSRL_API UFPSRLProgressionSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UFPSRLProgressionSet();

	ATTRIBUTE_ACCESSORS_BASIC(UFPSRLProgressionSet, MaxBoonSlots)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

protected:
	UFUNCTION()
	void OnRep_MaxBoonSlots(const FGameplayAttributeData& OldValue);

private:
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxBoonSlots, Category = "Progression", meta = (AllowPrivateAccess = "true"))
	FGameplayAttributeData MaxBoonSlots;
};
