// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Commands/InputChord.h"
#include "Types/SlateEnums.h"
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

	/** Tests: the Voice tab of the Settings page and what it shows ("voice on 1, volume 100%, ..."). */
	void ShowVoiceTab() { HandleShowVoiceTab(); }
	FString DescribeVoiceSettings() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
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

	/** Settings tabs (Chat, Voice). */
	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> SettingsTabs;

	// Voice tab (UFPSRLUserSettings voice settings, applied by UFPSRLVoiceSubsystem).
	UPROPERTY(Transient)
	TObjectPtr<class UCheckBox> VoiceEnabledCheck;
	UPROPERTY(Transient)
	TObjectPtr<class USlider> VoiceVolumeSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VoiceVolumeText;
	UPROPERTY(Transient)
	TObjectPtr<class USlider> MicVolumeSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MicVolumeText;
	UPROPERTY(Transient)
	TObjectPtr<class USlider> SensitivitySlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SensitivityText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MicCheckText;
	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> MicrophoneCombo;
	UPROPERTY(Transient)
	TObjectPtr<class UProgressBar> MicLevelBar;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetectText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VoiceTestText;
	FTimerHandle MicMeterTimer;
	FDelegateHandle DetectHandle;
	UPROPERTY(Transient)
	TObjectPtr<UButton> InputModeButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InputModeText;
	UPROPERTY(Transient)
	TObjectPtr<class UInputKeySelector> PushToTalkKeySelector;
	UPROPERTY(Transient)
	TObjectPtr<class UComboBoxString> OutputDeviceCombo;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> VoiceStatusText;
	UPROPERTY(Transient)
	TObjectPtr<class UVerticalBox> MuteList;
	/** Mute checkbox -> teammate PlayerId. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<class UCheckBox>, int32> MuteChecks;

private:
	void BuildDefaultLayout();
	UWidget* BuildSettingsPage();

	UFUNCTION()
	void HandleOpenSettings();

	UFUNCTION()
	void HandleCloseSettings();

	UFUNCTION()
	void HandleProfanityChanged(bool bIsChecked);

	UWidget* BuildChatSection();
	UWidget* BuildVoiceSection();
	/** Shows the current voice settings, devices, status and teammates on the Voice tab. */
	void RefreshVoiceTab();

	UFUNCTION()
	void HandleShowChatTab();
	UFUNCTION()
	void HandleShowVoiceTab();
	UFUNCTION()
	void HandleVoiceEnabledChanged(bool bIsChecked);
	UFUNCTION()
	void HandleVoiceVolumeChanged(float Value);
	UFUNCTION()
	void HandleMicVolumeChanged(float Value);
	UFUNCTION()
	void HandleSensitivityChanged(float Value);
	UFUNCTION()
	void HandleMicrophoneChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleAutoDetectClicked();
	UFUNCTION()
	void HandleVoiceTestClicked();
	FDelegateHandle VoiceTestHandle;
	/** The mic level meter (a 10 Hz UI refresh, only while the Voice tab is open). */
	void UpdateMicMeter();
	void StopMicMeter();
	/** "Mic check": whether the game hears this player right now (the voice capture's own activity detection). */
	void RefreshMicCheck();
	FDelegateHandle LocalSpeakingHandle;
	UFUNCTION()
	void HandleInputModeClicked();
	UFUNCTION()
	void HandlePushToTalkKeySelected(FInputChord SelectedKey);
	UFUNCTION()
	void HandleOutputDeviceChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleMuteChanged(bool bIsChecked);
	void RefreshBuildSummary();
	UButton* AddButton(UPanelWidget* Parent, const FName& Name, const FText& Label);

	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleQuitToMenu();

	UFUNCTION()
	void HandleQuitGame();
};
