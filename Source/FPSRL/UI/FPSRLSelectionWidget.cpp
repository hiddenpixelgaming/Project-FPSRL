// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLSelectionWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

namespace
{
	// Dark theme: near-black buttons, white text. Upgrades use a dark gold instead of the dark grey.
	const FLinearColor ButtonColor(0.07f, 0.07f, 0.09f);
	const FLinearColor GoldButtonColor(0.42f, 0.3f, 0.05f);
	const FLinearColor TextColor = FLinearColor::White;
	const FLinearColor DescriptionColor(0.78f, 0.78f, 0.8f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FName& Name, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetAutoWrapText(true);
		return Text;
	}

	UVerticalBoxSlot* AddTo(UVerticalBox* Box, UWidget* Child, float Padding)
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
		Slot->SetPadding(FMargin(0.f, Padding));
		Slot->SetHorizontalAlignment(HAlign_Fill);
		return Slot;
	}
}

void UFPSRLSelectionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	BindChoiceWidgets();

	if (RerollButton)
	{
		RerollButton->OnClicked.AddDynamic(this, &ThisClass::HandleReroll);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleClose);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &ThisClass::HandleBack);
	}
}

void UFPSRLSelectionWidget::BuildDefaultLayout()
{
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Backdrop->SetContent(Column);

	Title = MakeText(WidgetTree, TEXT("Title"), 36, FLinearColor::White);
	AddTo(Column, Title, 8.f)->SetHorizontalAlignment(HAlign_Center);

	Countdown = MakeText(WidgetTree, TEXT("Countdown"), 18, FLinearColor(1.f, 0.85f, 0.3f));
	AddTo(Column, Countdown, 4.f)->SetHorizontalAlignment(HAlign_Center);

	for (int32 Index = 0; Index < MaxChoices; ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("Choice%d"), Index));
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UTextBlock* Header = MakeText(WidgetTree, *FString::Printf(TEXT("ChoiceHeader%d"), Index), 13, TextColor);
		UTextBlock* Name = MakeText(WidgetTree, *FString::Printf(TEXT("ChoiceName%d"), Index), 22, TextColor);
		UTextBlock* Desc = MakeText(WidgetTree, *FString::Printf(TEXT("ChoiceDesc%d"), Index), 14, DescriptionColor);
		Content->AddChildToVerticalBox(Header);
		Content->AddChildToVerticalBox(Name);
		Content->AddChildToVerticalBox(Desc);
		Button->SetBackgroundColor(ButtonColor);
		Button->SetContent(Content);
		AddTo(Column, Button, 6.f);
	}

	RerollButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RerollButton"));
	RerollLabel = MakeText(WidgetTree, TEXT("RerollLabel"), 18, TextColor);
	RerollButton->SetBackgroundColor(ButtonColor);
	RerollButton->SetContent(RerollLabel);
	AddTo(Column, RerollButton, 14.f)->SetHorizontalAlignment(HAlign_Center);

	BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BackButton"));
	UTextBlock* BackLabel = MakeText(WidgetTree, TEXT("BackLabel"), 16, TextColor);
	BackButton->SetBackgroundColor(ButtonColor);
	BackLabel->SetText(NSLOCTEXT("FPSRL", "SelectionBack", "Back"));
	BackButton->SetContent(BackLabel);
	AddTo(Column, BackButton, 6.f)->SetHorizontalAlignment(HAlign_Center);

	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	UTextBlock* CloseLabel = MakeText(WidgetTree, TEXT("CloseLabel"), 16, TextColor);
	CloseButton->SetBackgroundColor(ButtonColor);
	CloseLabel->SetText(NSLOCTEXT("FPSRL", "SelectionClose", "Close (decide later)"));
	CloseButton->SetContent(CloseLabel);
	AddTo(Column, CloseButton, 6.f)->SetHorizontalAlignment(HAlign_Center);
}

void UFPSRLSelectionWidget::SetBackVisible(bool bVisible)
{
	if (BackButton)
	{
		BackButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UFPSRLSelectionWidget::SetCloseVisible(bool bVisible)
{
	if (CloseButton)
	{
		CloseButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UFPSRLSelectionWidget::BindChoiceWidgets()
{
	static const FName HandlerNames[MaxChoices] = { TEXT("HandleChoice0"), TEXT("HandleChoice1"), TEXT("HandleChoice2"), TEXT("HandleChoice3"), TEXT("HandleChoice4") };

	ChoiceButtons.Reset();
	ChoiceHeaders.Reset();
	ChoiceNames.Reset();
	ChoiceDescriptions.Reset();
	for (int32 Index = 0; Index < MaxChoices; ++Index)
	{
		UButton* Button = Cast<UButton>(GetWidgetFromName(*FString::Printf(TEXT("Choice%d"), Index)));
		if (Button)
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(this, HandlerNames[Index]);
			Button->OnClicked.AddUnique(Delegate);
		}
		ChoiceButtons.Add(Button);
		ChoiceHeaders.Add(Cast<UTextBlock>(GetWidgetFromName(*FString::Printf(TEXT("ChoiceHeader%d"), Index))));
		ChoiceNames.Add(Cast<UTextBlock>(GetWidgetFromName(*FString::Printf(TEXT("ChoiceName%d"), Index))));
		ChoiceDescriptions.Add(Cast<UTextBlock>(GetWidgetFromName(*FString::Printf(TEXT("ChoiceDesc%d"), Index))));
	}
}

void UFPSRLSelectionWidget::SetTitle(const FText& InTitle)
{
	if (Title)
	{
		Title->SetText(InTitle);
	}
}

void UFPSRLSelectionWidget::SetChoices(const TArray<FChoice>& Choices)
{
	for (int32 Index = 0; Index < MaxChoices; ++Index)
	{
		const FChoice* Choice = Choices.IsValidIndex(Index) ? &Choices[Index] : nullptr;
		if (UButton* Button = ChoiceButtons.IsValidIndex(Index) ? ChoiceButtons[Index].Get() : nullptr)
		{
			Button->SetVisibility(Choice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			Button->SetBackgroundColor(Choice && Choice->bGold ? GoldButtonColor : ButtonColor);
		}
		if (UTextBlock* Header = ChoiceHeaders.IsValidIndex(Index) ? ChoiceHeaders[Index].Get() : nullptr)
		{
			Header->SetText(Choice ? Choice->Header : FText::GetEmpty());
			// The Aspect's own colour reads well on the dark button.
			Header->SetColorAndOpacity(FSlateColor(Choice ? FLinearColor(Choice->HeaderColor.R, Choice->HeaderColor.G, Choice->HeaderColor.B, 1.f) : TextColor));
		}
		if (UTextBlock* Name = ChoiceNames.IsValidIndex(Index) ? ChoiceNames[Index].Get() : nullptr)
		{
			Name->SetText(Choice ? Choice->Name : FText::GetEmpty());
		}
		if (UTextBlock* Desc = ChoiceDescriptions.IsValidIndex(Index) ? ChoiceDescriptions[Index].Get() : nullptr)
		{
			Desc->SetText(Choice ? Choice->Description : FText::GetEmpty());
		}
	}
}

void UFPSRLSelectionWidget::SetReroll(bool bVisible, const FText& Label, bool bEnabled)
{
	if (RerollButton)
	{
		RerollButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		RerollButton->SetIsEnabled(bEnabled);
	}
	if (RerollLabel)
	{
		RerollLabel->SetText(Label);
	}
}

void UFPSRLSelectionWidget::SetDeadline(double ServerDeadline)
{
	Deadline = ServerDeadline;
	if (UWorld* World = GetWorld())
	{
		if (Deadline > 0.0)
		{
			World->GetTimerManager().SetTimer(CountdownTimer, this, &ThisClass::UpdateCountdown, 0.25f, true);
		}
		else
		{
			World->GetTimerManager().ClearTimer(CountdownTimer);
		}
	}
	if (Countdown)
	{
		Countdown->SetVisibility(Deadline > 0.0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	UpdateCountdown();
}

void UFPSRLSelectionWidget::UpdateCountdown()
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (Countdown && GameState && Deadline > 0.0)
	{
		const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Deadline - GameState->GetServerWorldTimeSeconds()));
		Countdown->SetText(FText::Format(NSLOCTEXT("FPSRL", "AutoPickIn", "Auto-pick in {0}s"), Seconds));
	}
}

void UFPSRLSelectionWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountdownTimer);
	}
	Super::NativeDestruct();
}

void UFPSRLSelectionWidget::HandleChoice0() { OnChoice.ExecuteIfBound(0); }
void UFPSRLSelectionWidget::HandleChoice1() { OnChoice.ExecuteIfBound(1); }
void UFPSRLSelectionWidget::HandleChoice2() { OnChoice.ExecuteIfBound(2); }
void UFPSRLSelectionWidget::HandleChoice3() { OnChoice.ExecuteIfBound(3); }
void UFPSRLSelectionWidget::HandleChoice4() { OnChoice.ExecuteIfBound(4); }
void UFPSRLSelectionWidget::HandleReroll() { OnReroll.ExecuteIfBound(); }
void UFPSRLSelectionWidget::HandleClose() { OnClose.ExecuteIfBound(); }
void UFPSRLSelectionWidget::HandleBack() { OnBack.ExecuteIfBound(); }
