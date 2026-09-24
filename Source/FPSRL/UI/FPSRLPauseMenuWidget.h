// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLPauseMenuWidget.generated.h"

class UButton;
class UPanelWidget;

/**
 * In-game pause overlay: Resume, Quit to Main Menu, Quit Game. Opened/closed by AFPSRLPlayerController (Esc / Start).
 *
 * Works with no Blueprint at all: if no designer layout is provided, it builds a simple default layout in code.
 * To restyle, make a Widget Blueprint deriving from this class and name its buttons ResumeButton,
 * QuitToMenuButton and QuitGameButton; they bind automatically (all optional).
 *
 * Does not pause the world: this is a listen-server co-op game, and pausing would freeze every player.
 * It only frees the mouse (so Alt+Tab and clicking work) while the run continues.
 */
UCLASS()
class FPSRL_API UFPSRLPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitToMenuButton;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitGameButton;

private:
	void BuildDefaultLayout();
	UButton* AddButton(UPanelWidget* Parent, const FName& Name, const FText& Label);

	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleQuitToMenu();

	UFUNCTION()
	void HandleQuitGame();
};
