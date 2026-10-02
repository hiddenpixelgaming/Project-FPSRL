// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLPauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
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
	if (UVerticalBoxSlot* SectionSlot = Page->AddChildToVerticalBox(Section))
	{
		SectionSlot->SetHorizontalAlignment(HAlign_Fill);
		SectionSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
	}

	SettingsBackButton = AddButton(Page, TEXT("SettingsBackButton"), LOCTEXT("Back", "Back"));
	return Page;
}

void UFPSRLPauseMenuWidget::HandleOpenSettings()
{
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

	if (UVerticalBoxSlot* ButtonSlot = Cast<UVerticalBox>(Parent)->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f, 6.f));
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

#undef LOCTEXT_NAMESPACE
