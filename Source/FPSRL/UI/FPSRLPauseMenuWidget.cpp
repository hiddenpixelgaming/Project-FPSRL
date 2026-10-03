// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLPauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/InputKeySelector.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "GameFramework/GameStateBase.h"
#include "Social/FPSRLVoiceSubsystem.h"
#include "Components/WidgetSwitcher.h"
#include "Social/FPSRLUserSettings.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLRelicComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "Data/FPSRLRelicDefinition.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/FPSRLPlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "FPSRLPauseMenu"

void UFPSRLPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);

	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddDynamic(this, &ThisClass::HandleResume);
	}
	if (QuitToMenuButton)
	{
		QuitToMenuButton->OnClicked.AddDynamic(this, &ThisClass::HandleQuitToMenu);
	}
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddDynamic(this, &ThisClass::HandleOpenSettings);
	}
	if (SettingsBackButton)
	{
		SettingsBackButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseSettings);
	}
	if (QuitGameButton)
	{
		QuitGameButton->OnClicked.AddDynamic(this, &ThisClass::HandleQuitGame);
	}
}

void UFPSRLPauseMenuWidget::BuildDefaultLayout()
{
	// Full-screen dimmed backdrop with a centered column.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	// Two pages: the main menu (buttons + build summary) and Settings.
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
	Backdrop->SetContent(Pages);

	// Menu buttons on the left, the build summary on the right.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Pages->AddChild(Row);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	if (UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Column))
	{
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
		ColumnSlot->SetPadding(FMargin(0.f, 0.f, 64.f, 0.f));
	}

	UBorder* BuildPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BuildPanel"));
	BuildPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.07f, 0.9f));
	BuildPanel->SetPadding(FMargin(24.f));
	BuildSummary = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BuildSummary"));
	FSlateFontInfo SummaryFont = BuildSummary->GetFont();
	SummaryFont.Size = 16;
	BuildSummary->SetFont(SummaryFont);
	BuildSummary->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)));
	BuildPanel->SetContent(BuildSummary);
	if (UHorizontalBoxSlot* PanelSlot = Row->AddChildToHorizontalBox(BuildPanel))
	{
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(LOCTEXT("Title", "PAUSED"));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 48;
	Title->SetFont(TitleFont);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
	}

	ResumeButton = AddButton(Column, TEXT("ResumeButton"), LOCTEXT("Resume", "Resume"));
	SettingsButton = AddButton(Column, TEXT("SettingsButton"), LOCTEXT("Settings", "Settings"));
	QuitToMenuButton = AddButton(Column, TEXT("QuitToMenuButton"), LOCTEXT("QuitToMenu", "Quit to Main Menu"));
	QuitGameButton = AddButton(Column, TEXT("QuitGameButton"), LOCTEXT("QuitGame", "Quit Game"));

	Pages->AddChild(BuildSettingsPage());
}

UWidget* UFPSRLPauseMenuWidget::BuildSettingsPage()
{
	// The start of the settings menu: a titled column of sections; Chat is the first.
	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsPage"));
	auto MakeText = [this](const FText& Value, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(Value);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		return Text;
	};
	if (UVerticalBoxSlot* TitleSlot = Page->AddChildToVerticalBox(MakeText(LOCTEXT("SettingsTitle", "SETTINGS"), 48, FLinearColor::White)))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 28.f));
	}

	// Tabs: Chat | Voice.
	UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsTabRow"));
	UButton* ChatTab = AddButton(TabRow, TEXT("ChatTabButton"), LOCTEXT("ChatTab", "Chat"));
	UButton* VoiceTab = AddButton(TabRow, TEXT("VoiceTabButton"), LOCTEXT("VoiceTab", "Voice"));
	ChatTab->OnClicked.AddDynamic(this, &ThisClass::HandleShowChatTab);
	VoiceTab->OnClicked.AddDynamic(this, &ThisClass::HandleShowVoiceTab);
	if (UVerticalBoxSlot* TabSlot = Page->AddChildToVerticalBox(TabRow))
	{
		TabSlot->SetHorizontalAlignment(HAlign_Center);
		TabSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}
	SettingsTabs = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("SettingsTabs"));
	SettingsTabs->AddChild(BuildChatSection());
	SettingsTabs->AddChild(BuildVoiceSection());
	if (UVerticalBoxSlot* TabsSlot = Page->AddChildToVerticalBox(SettingsTabs))
	{
		TabsSlot->SetHorizontalAlignment(HAlign_Fill);
		TabsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
	}

	SettingsBackButton = AddButton(Page, TEXT("SettingsBackButton"), LOCTEXT("Back", "Back"));
	return Page;
}

UWidget* UFPSRLPauseMenuWidget::BuildChatSection()
{
	auto MakeText = [this](const FText& Value, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(Value);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		return Text;
	};
	UBorder* Section = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChatSection"));
	Section->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.07f, 0.9f));
	Section->SetPadding(FMargin(24.f, 18.f));
	UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChatSectionLines"));
	Section->SetContent(Lines);
	Lines->AddChildToVerticalBox(MakeText(LOCTEXT("ChatHeading", "CHAT"), 18, FLinearColor(1.f, 0.85f, 0.45f)))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfanityRow"));
	ProfanityCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("ProfanityCheck"));
	ProfanityCheck->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleProfanityChanged);
	ProfanityCheck->SetRenderScale(FVector2D(1.6f, 1.6f));	// the default box is tiny next to 20 pt text
	if (UHorizontalBoxSlot* CheckSlot = Row->AddChildToHorizontalBox(ProfanityCheck))
	{
		CheckSlot->SetVerticalAlignment(VAlign_Center);
		CheckSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	}
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeText(LOCTEXT("ProfanityLabel", "Filter profanity in chat"), 20, FLinearColor(0.92f, 0.92f, 0.92f))))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	Lines->AddChildToVerticalBox(Row);
	Lines->AddChildToVerticalBox(MakeText(LOCTEXT("ProfanityHint", "Masks offensive words in messages you see. Only affects you."), 14, FLinearColor(0.65f, 0.65f, 0.65f)))
		->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	return Section;
}

UWidget* UFPSRLPauseMenuWidget::BuildVoiceSection()
{
	const FLinearColor LabelColour(0.92f, 0.92f, 0.92f);
	const FLinearColor HintColour(0.65f, 0.65f, 0.65f);
	auto MakeText = [this](const FText& Value, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(Value);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		return Text;
	};
	UBorder* Section = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VoiceSection"));
	Section->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.07f, 0.9f));
	Section->SetPadding(FMargin(24.f, 18.f));
	UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VoiceSectionLines"));
	Section->SetContent(Lines);
	Lines->AddChildToVerticalBox(MakeText(LOCTEXT("VoiceHeading", "VOICE"), 18, FLinearColor(1.f, 0.85f, 0.45f)))->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	// One row: a fixed-width label, then the control (and an optional value text after it).
	auto AddRow = [this, &Lines, &MakeText, LabelColour](const FText& Label, UWidget* Control, UTextBlock* Value = nullptr)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabelBox->SetWidthOverride(260.f);
		LabelBox->SetContent(MakeText(Label, 20, LabelColour));
		Row->AddChildToHorizontalBox(LabelBox)->SetVerticalAlignment(VAlign_Center);
		if (UHorizontalBoxSlot* ControlSlot = Row->AddChildToHorizontalBox(Control))
		{
			ControlSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (Value)
		{
			if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(Value))
			{
				ValueSlot->SetVerticalAlignment(VAlign_Center);
				ValueSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
			}
		}
		Lines->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 5.f));
	};
	auto MakeSlider = [this](const TCHAR* Name, float Max)
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(260.f);
		USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), Name);
		Slider->SetMinValue(0.f);
		Slider->SetMaxValue(Max);
		Slider->SetStepSize(0.05f);
		Slider->SetSliderBarColor(FLinearColor(0.3f, 0.3f, 0.35f));
		Slider->SetSliderHandleColor(FLinearColor(1.f, 0.85f, 0.45f));
		Box->SetContent(Slider);
		return TPair<USizeBox*, USlider*>(Box, Slider);
	};

	VoiceEnabledCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("VoiceEnabledCheck"));
	VoiceEnabledCheck->SetRenderScale(FVector2D(1.6f, 1.6f));
	VoiceEnabledCheck->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleVoiceEnabledChanged);
	AddRow(LOCTEXT("VoiceEnabled", "Voice chat"), VoiceEnabledCheck);

	const TPair<USizeBox*, USlider*> VoiceVolume = MakeSlider(TEXT("VoiceVolumeSlider"), 1.f);
	VoiceVolumeSlider = VoiceVolume.Value;
	VoiceVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleVoiceVolumeChanged);
	VoiceVolumeText = MakeText(FText::GetEmpty(), 18, LabelColour);
	AddRow(LOCTEXT("VoiceVolume", "Voice chat volume"), VoiceVolume.Key, VoiceVolumeText);

	const TPair<USizeBox*, USlider*> MicVolume = MakeSlider(TEXT("MicVolumeSlider"), 2.f);
	MicVolumeSlider = MicVolume.Value;
	MicVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleMicVolumeChanged);
	MicVolumeText = MakeText(FText::GetEmpty(), 18, LabelColour);
	AddRow(LOCTEXT("MicVolume", "Microphone volume"), MicVolume.Key, MicVolumeText);

	const TPair<USizeBox*, USlider*> Sensitivity = MakeSlider(TEXT("SensitivitySlider"), 1.f);
	SensitivitySlider = Sensitivity.Value;
	SensitivitySlider->OnValueChanged.AddDynamic(this, &ThisClass::HandleSensitivityChanged);
	SensitivityText = MakeText(FText::GetEmpty(), 18, LabelColour);
	AddRow(LOCTEXT("Sensitivity", "Open mic sensitivity"), Sensitivity.Key, SensitivityText);

	InputModeButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("InputModeButton"));
	InputModeButton->SetBackgroundColor(FLinearColor(0.2f, 0.2f, 0.24f));
	InputModeText = MakeText(FText::GetEmpty(), 18, FLinearColor::White);
	InputModeButton->SetContent(InputModeText);
	InputModeButton->OnClicked.AddDynamic(this, &ThisClass::HandleInputModeClicked);
	AddRow(LOCTEXT("InputMode", "Input mode"), InputModeButton);

	PushToTalkKeySelector = WidgetTree->ConstructWidget<UInputKeySelector>(UInputKeySelector::StaticClass(), TEXT("PushToTalkKeySelector"));
	PushToTalkKeySelector->SetAllowGamepadKeys(false);	// keyboard and mouse only (user)
	PushToTalkKeySelector->SetAllowModifierKeys(false);
	PushToTalkKeySelector->SetEscapeKeys({ EKeys::Escape });
	PushToTalkKeySelector->SetKeySelectionText(LOCTEXT("PressKey", "Press a key..."));
	PushToTalkKeySelector->OnKeySelected.AddDynamic(this, &ThisClass::HandlePushToTalkKeySelected);
	{
		FTextBlockStyle KeyStyle = PushToTalkKeySelector->GetTextStyle();
		KeyStyle.SetColorAndOpacity(FSlateColor(FLinearColor::Black));	// readable on the light key button
		KeyStyle.Font.Size = 18;
		PushToTalkKeySelector->SetTextStyle(KeyStyle);
		PushToTalkKeySelector->SetMargin(FMargin(18.f, 4.f));
	}
	AddRow(LOCTEXT("PushToTalkKey", "Push-to-Talk key"), PushToTalkKeySelector);

	// The engine's voice capture always records from the system's default device: say so instead of a fake choice.
	AddRow(LOCTEXT("Microphone", "Microphone"), MakeText(LOCTEXT("MicDefault", "System default (choose it in Windows Sound settings)"), 16, HintColour));

	OutputDeviceCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("OutputDeviceCombo"));
	OutputDeviceCombo->OnSelectionChanged.AddDynamic(this, &ThisClass::HandleOutputDeviceChanged);
	USizeBox* ComboBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ComboBox->SetWidthOverride(420.f);
	ComboBox->SetContent(OutputDeviceCombo);
	AddRow(LOCTEXT("OutputDevice", "Voice output device"), ComboBox);

	VoiceStatusText = MakeText(FText::GetEmpty(), 15, FLinearColor(1.f, 0.85f, 0.45f));
	Lines->AddChildToVerticalBox(VoiceStatusText)->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f));
	MicCheckText = MakeText(FText::GetEmpty(), 15, HintColour);
	Lines->AddChildToVerticalBox(MicCheckText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	Lines->AddChildToVerticalBox(MakeText(LOCTEXT("TeammatesHeading", "MUTE TEAMMATES (only you stop hearing them)"), 15, HintColour))->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f));
	MuteList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MuteList"));
	Lines->AddChildToVerticalBox(MuteList);
	return Section;
}

void UFPSRLPauseMenuWidget::RefreshVoiceTab()
{
	const UFPSRLUserSettings* Settings = UFPSRLUserSettings::Get();
	const UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(GetOwningPlayer());
	if (VoiceEnabledCheck)
	{
		VoiceEnabledCheck->SetIsChecked(Settings->bVoiceChatEnabled);
	}
	if (VoiceVolumeSlider)
	{
		VoiceVolumeSlider->SetValue(Settings->VoiceChatVolume);
		VoiceVolumeText->SetText(FText::AsPercent(Settings->VoiceChatVolume));
	}
	if (MicVolumeSlider)
	{
		MicVolumeSlider->SetValue(Settings->MicrophoneVolume);
		MicVolumeText->SetText(FText::AsPercent(Settings->MicrophoneVolume));
	}
	if (InputModeText)
	{
		InputModeText->SetText(Settings->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk ? LOCTEXT("ModePTT", "  Push to Talk  ") : LOCTEXT("ModeOpen", "  Open Mic  "));
	}
	if (PushToTalkKeySelector)
	{
		PushToTalkKeySelector->SetSelectedKey(FInputChord(Settings->PushToTalkKey));
		PushToTalkKeySelector->SetIsEnabled(Settings->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk);
	}
	if (OutputDeviceCombo)
	{
		// The game's own output first; devices only when the audio system can list them (otherwise no fake choice).
		const FString GameOutput = LOCTEXT("GameOutput", "Game audio output (default)").ToString();
		OutputDeviceCombo->ClearOptions();
		OutputDeviceCombo->AddOption(GameOutput);
		for (const FString& Device : Voice ? Voice->GetOutputDevices() : TArray<FString>())
		{
			OutputDeviceCombo->AddOption(Device);
		}
		const bool bKnown = !Settings->VoiceOutputDevice.IsEmpty() && OutputDeviceCombo->FindOptionIndex(Settings->VoiceOutputDevice) != INDEX_NONE;
		OutputDeviceCombo->SetSelectedOption(bKnown ? Settings->VoiceOutputDevice : GameOutput);
	}
	if (SensitivitySlider)
	{
		SensitivitySlider->SetValue(Settings->OpenMicSensitivity);
		SensitivityText->SetText(FText::AsPercent(Settings->OpenMicSensitivity));
		SensitivitySlider->SetIsEnabled(Settings->VoiceInputMode == EFPSRLVoiceInputMode::OpenMic);
	}
	if (VoiceStatusText)
	{
		VoiceStatusText->SetText(Voice ? Voice->GetStatusText() : LOCTEXT("NoVoice", "Voice chat is unavailable"));
	}
	if (UFPSRLVoiceSubsystem* MutableVoice = UFPSRLVoiceSubsystem::Get(GetOwningPlayer()); MutableVoice && !LocalSpeakingHandle.IsValid())
	{
		LocalSpeakingHandle = MutableVoice->OnLocalSpeakingChanged.AddWeakLambda(this, [this]() { RefreshMicCheck(); });
	}
	RefreshMicCheck();
	// Teammates (never this player), each with a local mute box.
	if (MuteList)
	{
		MuteList->ClearChildren();
		MuteChecks.Reset();
		const APlayerController* PC = GetOwningPlayer();
		const UWorld* World = PC ? PC->GetWorld() : nullptr;
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		TArray<APlayerState*> Others;
		for (APlayerState* Other : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
		{
			if (Other && Other != PC->PlayerState && !Other->IsABot() && !Other->IsInactive() && !Other->GetPlayerName().IsEmpty())
			{
				Others.Add(Other);
			}
		}
		Others.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });
		for (const APlayerState* Other : Others)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
			Check->SetRenderScale(FVector2D(1.4f, 1.4f));
			Check->SetIsChecked(Voice && Voice->IsMuted(Other->GetPlayerId()));
			Check->OnCheckStateChanged.AddDynamic(this, &ThisClass::HandleMuteChanged);
			MuteChecks.Add(Check, Other->GetPlayerId());
			Row->AddChildToHorizontalBox(Check)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Name->SetText(FText::Format(LOCTEXT("MuteName", "Mute {0}"), FText::FromString(Other->GetPlayerName())));
			FSlateFontInfo Font = Name->GetFont();
			Font.Size = 18;
			Name->SetFont(Font);
			Row->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Center);
			MuteList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 3.f));
		}
		if (Others.IsEmpty())
		{
			UTextBlock* None = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			None->SetText(LOCTEXT("NoTeammates", "No teammates in this session"));
			None->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)));
			MuteList->AddChildToVerticalBox(None);
		}
	}
}

void UFPSRLPauseMenuWidget::HandleShowChatTab()
{
	if (SettingsTabs)
	{
		SettingsTabs->SetActiveWidgetIndex(0);
	}
}

void UFPSRLPauseMenuWidget::HandleShowVoiceTab()
{
	if (Pages && Pages->GetActiveWidgetIndex() != 1)
	{
		HandleOpenSettings();
	}
	RefreshVoiceTab();
	if (SettingsTabs)
	{
		SettingsTabs->SetActiveWidgetIndex(1);
	}
}

void UFPSRLPauseMenuWidget::HandleVoiceEnabledChanged(bool bIsChecked)
{
	UFPSRLUserSettings::SetVoiceChatEnabled(bIsChecked);
	RefreshVoiceTab();
}

void UFPSRLPauseMenuWidget::HandleVoiceVolumeChanged(float Value)
{
	UFPSRLUserSettings::SetVoiceChatVolume(Value);
	if (VoiceVolumeText)
	{
		VoiceVolumeText->SetText(FText::AsPercent(UFPSRLUserSettings::Get()->VoiceChatVolume));
	}
}

void UFPSRLPauseMenuWidget::HandleMicVolumeChanged(float Value)
{
	UFPSRLUserSettings::SetMicrophoneVolume(Value);
	if (MicVolumeText)
	{
		MicVolumeText->SetText(FText::AsPercent(UFPSRLUserSettings::Get()->MicrophoneVolume));
	}
}

void UFPSRLPauseMenuWidget::HandleInputModeClicked()
{
	const bool bPushToTalk = UFPSRLUserSettings::Get()->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk;
	UFPSRLUserSettings::SetVoiceInputMode(bPushToTalk ? EFPSRLVoiceInputMode::OpenMic : EFPSRLVoiceInputMode::PushToTalk);
	RefreshVoiceTab();
}

void UFPSRLPauseMenuWidget::HandlePushToTalkKeySelected(FInputChord SelectedKey)
{
	if (SelectedKey.Key.IsValid() && !SelectedKey.Key.IsGamepadKey() && SelectedKey.Key != EKeys::Escape)
	{
		UFPSRLUserSettings::SetPushToTalkKey(SelectedKey.Key);
	}
	RefreshVoiceTab();
}

void UFPSRLPauseMenuWidget::HandleOutputDeviceChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct || !OutputDeviceCombo)
	{
		return;	// set from RefreshVoiceTab, not by the player
	}
	UFPSRLUserSettings::SetVoiceOutputDevice(OutputDeviceCombo->GetSelectedIndex() <= 0 ? FString() : SelectedItem);
}

void UFPSRLPauseMenuWidget::HandleMuteChanged(bool bIsChecked)
{
	// One handler for every row: apply each box's state (only the one just clicked differs).
	if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(GetOwningPlayer()))
	{
		for (const TPair<TObjectPtr<UCheckBox>, int32>& Pair : MuteChecks)
		{
			if (Pair.Key)
			{
				Voice->SetMuted(Pair.Value, Pair.Key->IsChecked());
			}
		}
	}
}

FString UFPSRLPauseMenuWidget::DescribeVoiceSettings() const
{
	TArray<FString> Mutes;
	for (const TPair<TObjectPtr<UCheckBox>, int32>& Pair : MuteChecks)
	{
		Mutes.Add(FString::Printf(TEXT("%d:%d"), Pair.Value, Pair.Key && Pair.Key->IsChecked()));
	}
	return FString::Printf(TEXT("tab %d, voice on %d, volume %s, mic %s, mode '%s', key %s (enabled %d), output '%s' of %d, status '%s', mute rows [%s]"),
		SettingsTabs ? SettingsTabs->GetActiveWidgetIndex() : -1, VoiceEnabledCheck && VoiceEnabledCheck->IsChecked(),
		VoiceVolumeText ? *VoiceVolumeText->GetText().ToString() : TEXT("?"), MicVolumeText ? *MicVolumeText->GetText().ToString() : TEXT("?"),
		InputModeText ? *InputModeText->GetText().ToString().TrimStartAndEnd() : TEXT("?"),
		PushToTalkKeySelector ? *PushToTalkKeySelector->GetSelectedKey().Key.ToString() : TEXT("?"), PushToTalkKeySelector && PushToTalkKeySelector->GetIsEnabled(),
		OutputDeviceCombo ? *OutputDeviceCombo->GetSelectedOption() : TEXT("?"), OutputDeviceCombo ? OutputDeviceCombo->GetOptionCount() : 0,
		VoiceStatusText ? *VoiceStatusText->GetText().ToString() : TEXT("?"), *FString::Join(Mutes, TEXT(" ")));
}

void UFPSRLPauseMenuWidget::HandleOpenSettings()
{
	RefreshVoiceTab();
	if (ProfanityCheck)
	{
		ProfanityCheck->SetIsChecked(UFPSRLUserSettings::Get()->bFilterProfanity);
	}
	if (Pages)
	{
		Pages->SetActiveWidgetIndex(1);
	}
}

void UFPSRLPauseMenuWidget::HandleCloseSettings()
{
	if (Pages)
	{
		Pages->SetActiveWidgetIndex(0);
	}
}

bool UFPSRLPauseMenuWidget::IsSettingsPageShown() const
{
	return Pages && Pages->GetActiveWidgetIndex() == 1;
}

void UFPSRLPauseMenuWidget::HandleProfanityChanged(bool bIsChecked)
{
	UFPSRLUserSettings::SetFilterProfanity(bIsChecked);
}

UButton* UFPSRLPauseMenuWidget::AddButton(UPanelWidget* Parent, const FName& Name, const FText& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(Label);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 24;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	Button->SetContent(Text);

	UPanelSlot* PanelSlot = Parent ? Parent->AddChild(Button) : nullptr;
	if (UVerticalBoxSlot* ButtonSlot = Cast<UVerticalBoxSlot>(PanelSlot))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f, 6.f));
	}
	else if (UHorizontalBoxSlot* TabSlot = Cast<UHorizontalBoxSlot>(PanelSlot))	// a row of tabs
	{
		TabSlot->SetPadding(FMargin(6.f, 0.f));
		Text->SetText(FText::Format(INVTEXT("    {0}    "), Label));
	}
	return Button;
}

FReply UFPSRLPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// While open, input is UI-only, so the pause key arrives here rather than through Enhanced Input.
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right)
	{
		// From Settings, back to the main page; from the main page, resume.
		if (Pages && Pages->GetActiveWidgetIndex() != 0)
		{
			HandleCloseSettings();
		}
		else if (Key != EKeys::Gamepad_FaceButton_Right)
		{
			HandleResume();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UFPSRLPauseMenuWidget::HandleResume()
{
	if (AFPSRLPlayerController* PC = GetOwningPlayer<AFPSRLPlayerController>())
	{
		PC->ClosePauseMenu();
	}
}

void UFPSRLPauseMenuWidget::HandleQuitToMenu()
{
	if (AFPSRLPlayerController* PC = GetOwningPlayer<AFPSRLPlayerController>())
	{
		PC->QuitToMainMenu();
	}
}

void UFPSRLPauseMenuWidget::HandleQuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UFPSRLPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshBuildSummary();	// every time the menu opens
	HandleCloseSettings();	// always opens on the main page
}

void UFPSRLPauseMenuWidget::RefreshBuildSummary()
{
	if (!BuildSummary)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	const AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	if (!PS)
	{
		BuildSummary->SetText(FText::GetEmpty());
		return;
	}

	// Placeholder text layout (one star per upgrade level on a Blessing); the UI pass will replace it.
	auto NameOf = [](const FText& DisplayName, const UObject* Asset) { return DisplayName.IsEmpty() ? GetNameSafe(Asset) : DisplayName.ToString(); };
	const UFPSRLBoonComponent* Boons = PS->GetBoonComponent();
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const FString Star = TEXT(" ★");

	FString Text = TEXT("YOUR BUILD\n");
	Text += FString::Printf(TEXT("Free rerolls left this run: %d / %d\n"), Boons->FreeRerollsRemaining, Settings.FreeRerollsPerRun);
	for (const FFPSRLBoonTrack& Track : Boons->Tracks)
	{
		const FGameplayTag Item = Boons->GetChannelItem(Track.Channel);
		FString ItemName;
		Item.GetTagName().ToString().Split(TEXT("."), nullptr, &ItemName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		Text += FString::Printf(TEXT("\n%s  (%s)\n"), *UFPSRLBoonComponent::GetChannelName(Track.Channel).ToString().ToUpper(),
			Item.IsValid() ? *ItemName : TEXT("nothing equipped"));
		if (!Track.Aspect)
		{
			if (Item.IsValid())
			{
				Text += TEXT("   No Aspect yet\n");
			}
			continue;
		}
		Text += FString::Printf(TEXT("   %s Aspect  -  %d / %d Blessings\n"), *NameOf(Track.Aspect->DisplayName, Track.Aspect),
			Track.Count, Settings.MaxBoonsPerChannel);
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			if (!Owned.Boon)
			{
				continue;
			}
			const TCHAR* Kind = Owned.Boon->BoonType == EFPSRLBoonType::Major ? TEXT("MAJOR: ") : Owned.Boon->BoonType == EFPSRLBoonType::Minor ? TEXT("MINOR: ") : TEXT("");
			const FString Stacks = Owned.Stacks > 1 ? FString::Printf(TEXT(" x%d"), Owned.Stacks) : FString();
			// Upgrades stack: one star per upgrade level.
			FString Upgrades;
			for (int32 Level = 0; Level < Owned.UpgradeLevel; ++Level)
			{
				Upgrades += Level == 0 ? Star : Star.TrimStart();
			}
			Text += FString::Printf(TEXT("   - %s%s%s%s\n"), Kind, *NameOf(Owned.Boon->DisplayName, Owned.Boon), *Stacks, *Upgrades);
		}
	}

	Text += TEXT("\nRELICS\n");
	const TArray<FFPSRLOwnedRelic>& Relics = PS->GetRelicComponent()->OwnedRelics;
	if (Relics.IsEmpty())
	{
		Text += TEXT("   None\n");
	}
	for (const FFPSRLOwnedRelic& Owned : Relics)
	{
		if (Owned.Relic)
		{
			const FString Stacks = Owned.Stacks > 1 ? FString::Printf(TEXT(" x%d"), Owned.Stacks) : FString();
			Text += FString::Printf(TEXT("   - %s (%s)%s\n"), *NameOf(Owned.Relic->DisplayName, Owned.Relic),
				*UEnum::GetDisplayValueAsText(Owned.Relic->Rarity).ToString(), *Stacks);
		}
	}
	BuildSummary->SetText(FText::FromString(Text));
}


void UFPSRLPauseMenuWidget::HandleSensitivityChanged(float Value)
{
	UFPSRLUserSettings::SetOpenMicSensitivity(Value);
	if (SensitivityText)
	{
		SensitivityText->SetText(FText::AsPercent(UFPSRLUserSettings::Get()->OpenMicSensitivity));
	}
}

void UFPSRLPauseMenuWidget::RefreshMicCheck()
{
	if (!MicCheckText)
	{
		return;
	}
	const UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(GetOwningPlayer());
	const UFPSRLUserSettings* Settings = UFPSRLUserSettings::Get();
	if (!Voice || Voice->GetStatus() != EFPSRLVoiceStatus::Ready)
	{
		MicCheckText->SetText(FText::GetEmpty());
		return;
	}
	if (Voice->IsLocalSpeaking())
	{
		MicCheckText->SetText(LOCTEXT("MicHeard", "Mic check: we hear you"));
		MicCheckText->SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 1.f, 0.4f)));
	}
	else
	{
		MicCheckText->SetText(Settings->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk
			? LOCTEXT("MicCheckPTT", "Mic check: switch to Open Mic to test your microphone here")
			: LOCTEXT("MicCheckOpen", "Mic check: talk now. If this never turns green, raise the sensitivity or microphone volume, or allow microphone access for desktop apps in Windows privacy settings"));
		MicCheckText->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)));
	}
	MicCheckText->SetWrapTextAt(640.f);
}

#undef LOCTEXT_NAMESPACE
