// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLCombatHUDWidget.generated.h"

class AFPSRLPlayerController;
class AFPSRLWeapon;
class UFPSRLHealthComponent;
class UImage;
class UProgressBar;
class USizeBox;
class UTextBlock;

/**
 * The player's combat HUD (created by AFPSRLPlayerController for the local player; default layout built in code):
 *  - bottom left: health bar
 *  - bottom right: ammo counter, with the melee cooldown square to its left (a shade that recedes top-down as it refreshes)
 *  - under the crosshair: reload progress, only while reloading
 *  - full screen: a brief translucent red flash when the player takes damage
 * Event-driven (health / weapon events, a slow re-bind check); a fast timer runs only while the melee shade, the reload
 * bar or the damage flash is moving.
 */
UCLASS()
class FPSRL_API UFPSRLCombatHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetController(AFPSRLPlayerController* InController);

	/** The player just swung (starts the melee square's cooldown shade at once). */
	void NotifyMeleeSwung() { StartAnimating(); }

	/** Test: what the HUD shows right now, as text. */
	FString DescribeForTest() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmmoText;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> MeleeShade;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ReloadBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageFlash;

	/** Size of the melee square (the shade's full height). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float MeleeIconSize = 64.f;

	/** Opacity the damage flash starts at, and how long it takes to fade. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float DamageFlashOpacity = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float DamageFlashDuration = 0.3f;

private:
	void BuildDefaultLayout();
	void CheckBindings();
	void RefreshAmmo();
	void Animate();
	void StartAnimating();

	UFUNCTION()
	void HandleHealthChanged(double CurrentHealth, double MaxHealth);

	TWeakObjectPtr<AFPSRLPlayerController> Controller;
	TWeakObjectPtr<UFPSRLHealthComponent> BoundHealth;
	TWeakObjectPtr<AFPSRLWeapon> BoundWeapon;
	FDelegateHandle WeaponHandle;
	FTimerHandle BindTimer;
	FTimerHandle AnimTimer;
	double LastHealth = -1.0;
	double FlashStartTime = -1000.0;
};
