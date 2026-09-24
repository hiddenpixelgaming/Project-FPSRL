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
}

void UFPSRLSelectionWidget::BuildDefaultLayout()
{
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
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
		UTextBlock* Name = MakeText(WidgetTree, *FString::Printf(TEXT("ChoiceName%d"), Index), 22, FLinearColor::Black);
		UTextBlock* Desc = MakeText(WidgetTree, *FString::Printf(TEXT("ChoiceDesc%d"), Index), 14, FLinearColor(0.15f, 0.15f, 0.15f));
		Content->AddChildToVerticalBox(Name);
		Content->AddChildToVerticalBox(Desc);
		Button->SetContent(Content);
		AddTo(Column, Button, 6.f);
	}

	RerollButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RerollButton"));
	RerollLabel = MakeText(WidgetTree, TEXT("RerollLabel"), 18, FLinearColor::Black);
	RerollButton->SetContent(RerollLabel);
	AddTo(Column, RerollButton, 14.f)->SetHorizontalAlignment(HAlign_Center);
}

void UFPSRLSelectionWidget::BindChoiceWidgets()
{
	static const FName HandlerNames[MaxChoices] = { TEXT("HandleChoice0"), TEXT("HandleChoice1"), TEXT("HandleChoice2"), TEXT("HandleChoice3"), TEXT("HandleChoice4") };

	ChoiceButtons.Reset();
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

void UFPSRLSelectionWidget::SetChoices(const TArray<FText>& Names, const TArray<FText>& Descriptions)
{
	for (int32 Index = 0; Index < MaxChoices; ++Index)
	{
		const bool bUsed = Names.IsValidIndex(Index);
		if (UButton* Button = ChoiceButtons.IsValidIndex(Index) ? ChoiceButtons[Index].Get() : nullptr)
		{
			Button->SetVisibility(bUsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		if (UTextBlock* Name = ChoiceNames.IsValidIndex(Index) ? ChoiceNames[Index].Get() : nullptr)
		{
			Name->SetText(bUsed ? Names[Index] : FText::GetEmpty());
		}
		if (UTextBlock* Desc = ChoiceDescriptions.IsValidIndex(Index) ? ChoiceDescriptions[Index].Get() : nullptr)
		{
			Desc->SetText(Descriptions.IsValidIndex(Index) ? Descriptions[Index] : FText::GetEmpty());
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
