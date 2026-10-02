// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/FPSRLTypes.h"
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

	/** The dash key was pressed (the dash square watches the character's cooldown from here). */
	void NotifyDashPressed() { StartAnimating(); }

	/** One of this player's hits landed (or a swing missed): flash the hit marker. */
	void ShowHitFeedback(EFPSRLHitFeedback Kind);

	/** A floating damage number above the target at WorldLocation: white, yellow when critical; rises and fades. */
	void ShowDamageNumber(const FVector& WorldLocation, float Damage, bool bCritical);

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
	TObjectPtr<USizeBox> DashShade;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ReloadBar;

	UPROPERTY(BlueprintReadOnly, Category = "HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageFlash;

	/** Size of the ability squares (dash, melee; the shade's full height). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float MeleeIconSize = 64.f;

	/** Fixed width of the ammo counter's slot, so the ability squares never move. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float AmmoSlotWidth = 170.f;

	/** Opacity the damage flash starts at, and how long it takes to fade. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float DamageFlashOpacity = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	float DamageFlashDuration = 0.3f;

	/** Damage numbers: how long one stays up, how far it rises (cm, in the world), its colours and font size. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	float DamageNumberDuration = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	float DamageNumberRise = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	FLinearColor DamageNumberColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	FLinearColor CriticalDamageNumberColor = FLinearColor(1.f, 0.85f, 0.1f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	int32 DamageNumberFontSize = 20;

	/** At most this many numbers on screen; the oldest is reused when more land at once. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Damage Numbers")
	int32 MaxDamageNumbers = 24;

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
	/** When the character's dash went on cooldown (bCanDash false); -1 while ready. */
	double DashLockStart = -1.0;

	/** The dash is character Blueprint logic: its bCanDash / DashCooldown, read by name (-1 if missing). */
	float GetDashCooldownFraction();

	/** Melee reach brackets around the crosshair (an enemy is within reach in front), checked on a slow timer. */
	void RefreshReach();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> MarkerLines;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReachText;

	FTimerHandle ReachTimer;

	/** One floating damage number (a pooled text block). */
	struct FDamageNumber
	{
		TWeakObjectPtr<UTextBlock> Text;
		FVector WorldLocation = FVector::ZeroVector;
		double StartTime = -1000.0;
		bool bCritical = false;
	};
	TArray<FDamageNumber> DamageNumbers;

	/** Moves and fades the damage numbers; returns true while any is still showing. */
	bool AnimateDamageNumbers();

	UPROPERTY(Transient)
	TObjectPtr<class UCanvasPanel> RootCanvas;

	FString LastDamageNumberText;
	double MarkerStartTime = -1000.0;
	EFPSRLHitFeedback MarkerKind = EFPSRLHitFeedback::Hit;
	int32 FeedbackCount = 0;
};
