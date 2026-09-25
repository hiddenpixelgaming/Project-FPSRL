// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLPortalWidgets.generated.h"

class AFPSRLExitPortal;
class UButton;
class UTextBlock;

/**
 * Continue / Cancel dialog opened by interacting with an exit portal. Presentation only: it shows the portal's
 * replicated vote state and reports the button; AFPSRLPlayerController sends the vote and the server decides.
 * Builds a default layout in code; a Blueprint subclass may supply its own with widgets named Title, Body, Tracker,
 * ContinueButton, CancelButton (all optional).
 */
UCLASS()
class FPSRL_API UFPSRLPortalMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Refresh from the portal (destination, how many are ready, whether the local player already chose Continue). */
	void ShowPortal(const AFPSRLExitPortal* Portal, bool bLocalPlayerContinuing);

	FSimpleDelegate OnContinue;
	FSimpleDelegate OnCancel;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Body;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Tracker;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ContinueButton;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

private:
	void BuildDefaultLayout();

	UFUNCTION() void HandleContinue();
	UFUNCTION() void HandleCancel();
};

/**
 * Non-interactive HUD line shown to every player while anyone has chosen Continue:
 * "Continue to next Depth: 1 / 3 ready - everyone moves in 23s". The countdown refreshes on a 4 Hz timer, not Tick.
 */
UCLASS()
class FPSRL_API UFPSRLPortalStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowPortal(const AFPSRLExitPortal* Portal);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Portal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

private:
	void UpdateText();

	TWeakObjectPtr<const AFPSRLExitPortal> Portal;
	FTimerHandle RefreshTimer;
};

namespace FPSRLPortalText
{
	/** "Continue to the next Depth?", "Leave for the next Area?", ... */
	FText GetQuestion(const AFPSRLExitPortal* Portal);

	/** "1 / 3 ready - everyone moves in 23s" (or the threshold hint when no countdown runs). */
	FText GetTracker(const AFPSRLExitPortal* Portal);
}
