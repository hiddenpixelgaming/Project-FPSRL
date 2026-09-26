// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLReviveWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * Revive progress bar, shown to the reviver and to the player being revived (AFPSRLReviveMarker drives it through
 * AFPSRLPlayerController). Fills from the server's revive start to end time on a 20 Hz timer, not Tick.
 * Builds a default layout in code; a Blueprint subclass may supply its own with widgets named Label and Progress.
 */
UCLASS()
class FPSRL_API UFPSRLReviveWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Start (or restart) the bar. Times are server world seconds (AGameStateBase::GetServerWorldTimeSeconds). */
	void ShowRevive(const FText& InLabel, double InStartTime, double InEndTime);

	/** Stop the bar. With a message it stays up briefly showing it ("Revive interrupted"), otherwise it hides at once. */
	void HideRevive(const FText& Message = FText::GetEmpty());

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Revive", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Label;

	UPROPERTY(BlueprintReadOnly, Category = "Revive", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Progress;

private:
	void UpdateProgress();
	void HideNow();

	double StartTime = 0.0;
	double EndTime = 0.0;
	FTimerHandle RefreshTimer;
	FTimerHandle HideTimer;
};
