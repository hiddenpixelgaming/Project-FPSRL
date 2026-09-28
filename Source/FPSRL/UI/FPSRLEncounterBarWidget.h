// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLEncounterBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UFPSRLHealthComponent;

/**
 * The Elite / Final Level Boss health bar at the top of the screen, shown while such an encounter runs
 * (AFPSRLPlayerController::RefreshEncounterBar). Label and look come from the encounter's data; the bar follows the
 * encounter enemy's health through its health component's events (no Tick). Default layout built in code.
 */
UCLASS()
class FPSRL_API UFPSRLEncounterBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Follow this enemy's health, labelled Label, styled by Kind (Elite / Final Level Boss). */
	void SetTarget(UFPSRLHealthComponent* InHealth, const FText& Label, EFPSRLEncounterKind Kind);

	void ClearTarget();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Encounter", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "Encounter", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

private:
	void BuildDefaultLayout();

	UFUNCTION()
	void HandleHealthChanged(double CurrentHealth, double MaxHealth);

	TWeakObjectPtr<UFPSRLHealthComponent> Health;
};
