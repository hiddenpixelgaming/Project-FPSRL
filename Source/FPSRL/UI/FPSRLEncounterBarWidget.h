// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLEncounterBarWidget.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;
class UFPSRLHealthComponent;
class UFPSRLShieldEncounterComponent;

/**
 * The Miniboss / Final Level Boss health bar at the top of the screen, shown while such an encounter runs
 * (AFPSRLPlayerController::RefreshEncounterBar). Label and look come from the encounter's data; the bar follows the
 * encounter enemy's health through its health component's events (no Tick). Default layout built in code.
 *
 * A boss with a shield (UFPSRLShieldEncounterComponent, the Ground Juggernaut) shows SHIELD UP / DOWN before its health,
 * its threshold markers on the health bar (faded once reached), and the SHIELD DISRUPTION bar under it while the
 * platform mechanic runs. Driven by the component's replicated state events.
 */
UCLASS()
class FPSRL_API UFPSRLEncounterBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Follow this enemy's health, labelled Label, styled by Kind (Miniboss / Final Level Boss). */
	void SetTarget(UFPSRLHealthComponent* InHealth, const FText& Label, EFPSRLEncounterKind Kind);

	void ClearTarget();

	/** What it shows now (tests). */
	FString Describe() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Encounter", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "Encounter", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

private:
	void BuildDefaultLayout();
	void RefreshShields();

	UFUNCTION()
	void HandleHealthChanged(double CurrentHealth, double MaxHealth);

	TWeakObjectPtr<UFPSRLHealthComponent> Health;
	TWeakObjectPtr<UFPSRLShieldEncounterComponent> Shields;
	FDelegateHandle ShieldHandle;
	FLinearColor HealthColor = FLinearColor::White;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ShieldBoxes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ShieldTexts;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DisruptionText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> DisruptionBar;

	/** The threshold markers over the health bar (rebuilt when the thresholds change). */
	UPROPERTY(Transient)
	TObjectPtr<class UHorizontalBox> Markers;

	UPROPERTY(Transient)
	TMap<float, TObjectPtr<UBorder>> MarkerBorders;

	TArray<float> MarkersShown;
};
