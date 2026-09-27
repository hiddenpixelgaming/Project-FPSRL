// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLPauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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

	// Menu buttons on the left, the build summary on the right.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Backdrop->SetContent(Row);

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
	QuitToMenuButton = AddButton(Column, TEXT("QuitToMenuButton"), LOCTEXT("QuitToMenu", "Quit to Main Menu"));
	QuitGameButton = AddButton(Column, TEXT("QuitGameButton"), LOCTEXT("QuitGame", "Quit Game"));
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
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		HandleResume();
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

	// Placeholder text layout (a star marks upgraded Aspects and Blessings); the UI pass will replace it.
	auto NameOf = [](const FText& DisplayName, const UObject* Asset) { return DisplayName.IsEmpty() ? GetNameSafe(Asset) : DisplayName.ToString(); };
	const UFPSRLBoonComponent* Boons = PS->GetBoonComponent();
	const UFPSRLBoonSettings& Settings = UFPSRLBoonSettings::Get();
	const FString Star = TEXT(" ★");

	FString Text = TEXT("YOUR BUILD\n");
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
		Text += FString::Printf(TEXT("   %s Aspect%s  -  %d / %d Blessings\n"), *NameOf(Track.Aspect->DisplayName, Track.Aspect),
			Track.AspectUpgradeLevel > 0 ? *Star : TEXT(""), Track.Count, Settings.MaxBoonsPerChannel);
		if (Track.Count < Settings.MaxBoonsPerChannel)
		{
			// Makes the milestones visible: position 3 is always the Minor, 6 the Major.
			const EFPSRLBoonType Next = Settings.GetBoonTypeForPosition(Track.Count + 1);
			Text += FString::Printf(TEXT("   Next: Blessing %d%s\n"), Track.Count + 1,
				Next == EFPSRLBoonType::Minor ? TEXT(" = MINOR BLESSING") : Next == EFPSRLBoonType::Major ? TEXT(" = MAJOR BLESSING") : TEXT(""));
		}
		for (const FFPSRLOwnedBoon& Owned : Track.Boons)
		{
			if (!Owned.Boon)
			{
				continue;
			}
			const TCHAR* Kind = Owned.Boon->BoonType == EFPSRLBoonType::Minor ? TEXT("MINOR: ")
				: Owned.Boon->BoonType == EFPSRLBoonType::Major ? TEXT("MAJOR: ") : TEXT("");
			const FString Stacks = Owned.Stacks > 1 ? FString::Printf(TEXT(" x%d"), Owned.Stacks) : FString();
			Text += FString::Printf(TEXT("   - %s%s%s%s\n"), Kind, *NameOf(Owned.Boon->DisplayName, Owned.Boon), *Stacks,
				Owned.UpgradeLevel > 0 ? *Star : TEXT(""));
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
