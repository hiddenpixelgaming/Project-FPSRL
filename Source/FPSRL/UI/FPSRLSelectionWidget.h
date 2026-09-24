// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLSelectionWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

DECLARE_DELEGATE_OneParam(FFPSRLChoiceDelegate, int32 /*OptionIndex*/);

/**
 * Generic "pick one" screen used for Aspect selection (Lobby) and Boon selection (after an arena).
 * Presentation only: it shows what AFPSRLPlayerController gives it and reports which button was clicked;
 * the server decides what is valid. Builds a simple default layout in code; a Blueprint subclass can supply its own
 * layout with widgets named Title, Countdown, Choice0..Choice4 (buttons), ChoiceName0..4 / ChoiceDesc0..4 (text),
 * RerollButton and RerollLabel (all optional).
 * The countdown refreshes on a 4 Hz timer, not Tick.
 */
UCLASS()
class FPSRL_API UFPSRLSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxChoices = 5;

	void SetTitle(const FText& InTitle);
	void SetChoices(const TArray<FText>& Names, const TArray<FText>& Descriptions);
	void SetReroll(bool bVisible, const FText& Label, bool bEnabled);

	/** Server world time when the selection auto-resolves; 0 hides the countdown. */
	void SetDeadline(double ServerDeadline);

	FFPSRLChoiceDelegate OnChoice;
	FSimpleDelegate OnReroll;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "Selection", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "Selection", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Countdown;

	UPROPERTY(BlueprintReadOnly, Category = "Selection", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RerollButton;

	UPROPERTY(BlueprintReadOnly, Category = "Selection", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RerollLabel;

private:
	void BuildDefaultLayout();
	void BindChoiceWidgets();
	void UpdateCountdown();

	UFUNCTION() void HandleChoice0();
	UFUNCTION() void HandleChoice1();
	UFUNCTION() void HandleChoice2();
	UFUNCTION() void HandleChoice3();
	UFUNCTION() void HandleChoice4();
	UFUNCTION() void HandleReroll();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> ChoiceButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ChoiceNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ChoiceDescriptions;

	double Deadline = 0.0;
	FTimerHandle CountdownTimer;
};
