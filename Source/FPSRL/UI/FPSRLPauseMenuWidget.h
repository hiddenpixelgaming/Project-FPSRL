// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLPauseMenuWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;

/**
 * In-game pause overlay: Resume, Quit to Main Menu, Quit Game, and the player's build summary (Blessings and relics). Opened/closed by AFPSRLPlayerController (Esc / Start).
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

public:
	/** Tests: the Settings page (open / close) and its profanity checkbox, as a player would use them. */
	void ShowSettingsPage(bool bShow) { bShow ? HandleOpenSettings() : HandleCloseSettings(); }
	bool IsSettingsPageShown() const;
	class UCheckBox* GetProfanityCheck() const { return ProfanityCheck; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitToMenuButton;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitGameButton;

	/** "YOUR BUILD": each channel's Aspect and Blessings (upgraded ones starred) and the relics. Refreshed on open. */
	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BuildSummary;

	/** Settings screen (swapped in for the main column): the player's own preferences, saved per machine. */
	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> Pages;

	UPROPERTY(Transient)
	TObjectPtr<class UCheckBox> ProfanityCheck;

	UPROPERTY(Transient)
	TObjectPtr<UButton> SettingsBackButton;

private:
	void BuildDefaultLayout();
	UWidget* BuildSettingsPage();

	UFUNCTION()
	void HandleOpenSettings();

	UFUNCTION()
	void HandleCloseSettings();

	UFUNCTION()
	void HandleProfanityChanged(bool bIsChecked);
	void RefreshBuildSummary();
	UButton* AddButton(UPanelWidget* Parent, const FName& Name, const FText& Label);

	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleQuitToMenu();

	UFUNCTION()
	void HandleQuitGame();
};
