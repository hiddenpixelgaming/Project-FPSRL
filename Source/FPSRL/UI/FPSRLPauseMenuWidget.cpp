// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLPauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Backdrop->SetContent(Column);

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

#undef LOCTEXT_NAMESPACE
