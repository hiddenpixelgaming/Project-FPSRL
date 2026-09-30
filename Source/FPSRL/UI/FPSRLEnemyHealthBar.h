// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "GameplayTagContainer.h"
#include "FPSRLEnemyHealthBar.generated.h"

class UAbilitySystemComponent;
class UFPSRLHealthComponent;
class UProgressBar;

/** The bar itself (built in C++): a thin red fill on a dark back. */
UCLASS()
class FPSRL_API UFPSRLEnemyHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetFraction(float Fraction);
	float GetFraction() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> Bar;
};

/**
 * Health bar floating above an ordinary enemy's head (every machine, local UI). Added at runtime by the enemy's
 * UFPSRLHealthComponent, so no enemy Blueprint needs it. Minibosses and Final Level Bosses (Enemy.Rank.* on their ability
 * system, replicated) never show one.
 *
 * Updates on health change only. Screen-space widgets need the component's tick to follow the enemy on screen, so the
 * tick runs only while the bar is shown (hidden at full health, when dead, and for ranked enemies).
 * Mode: fpsrl.EnemyHealthBars (0 off, 1 once damaged, 2 always).
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLEnemyHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UFPSRLEnemyHealthBarComponent();

	/** Create and attach one above this enemy (no-op on a dedicated server). */
	static UFPSRLEnemyHealthBarComponent* AddTo(APawn* Enemy, UFPSRLHealthComponent* Health, UAbilitySystemComponent* ASC);

	/** Shown right now (tests). */
	bool IsBarShown() const { return bShown; }
	float GetShownFraction() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleHealthChanged(double CurrentHealth, double MaxHealth);

	void HandleRankTagChanged(const FGameplayTag Tag, int32 NewCount);
	void Refresh();

	TWeakObjectPtr<UFPSRLHealthComponent> Health;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle MinibossHandle;
	FDelegateHandle BossHandle;
	bool bShown = false;
};
