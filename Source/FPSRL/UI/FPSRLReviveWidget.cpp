// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLReviveWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

void UFPSRLReviveWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);	// never takes input
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// Centered column, a little below the crosshair.
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	WidgetTree->RootWidget = Column;

	UVerticalBox* Spacer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TopSpacer"));
	Column->AddChildToVerticalBox(Spacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 20;
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 1.f, 0.55f)));
	Label->SetShadowOffset(FVector2D(1.f, 1.f));
	Label->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* LabelSlot = Column->AddChildToVerticalBox(Label);
	LabelSlot->SetHorizontalAlignment(HAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarSize"));
	BarSize->SetWidthOverride(360.f);
	BarSize->SetHeightOverride(18.f);
	Progress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Progress"));
	Progress->SetFillColorAndOpacity(FLinearColor(0.2f, 0.9f, 0.3f));
	Progress->SetPercent(0.f);
	BarSize->SetContent(Progress);
	UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(BarSize);
	BarSlot->SetHorizontalAlignment(HAlign_Center);

	UVerticalBox* BottomSpacer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BottomSpacer"));
	FSlateChildSize BottomShare(ESlateSizeRule::Fill);
	BottomShare.Value = 0.6f;	// less room below than above: the bar sits under the crosshair
	Column->AddChildToVerticalBox(BottomSpacer)->SetSize(BottomShare);
}

void UFPSRLReviveWidget::ShowRevive(const FText& InLabel, double InStartTime, double InEndTime)
{
	StartTime = InStartTime;
	EndTime = InEndTime;
	if (Label)
	{
		Label->SetText(InLabel);
	}
	if (Progress)
	{
		Progress->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
		World->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::UpdateProgress, 0.05f, true);
	}
	UpdateProgress();
	if (!IsInViewport())
	{
		AddToViewport(15);
	}
}

void UFPSRLReviveWidget::HideRevive(const FText& Message)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (Message.IsEmpty() || !World)
	{
		HideNow();
		return;
	}

	// Brief message in place of the bar.
	if (Label)
	{
		Label->SetText(Message);
	}
	if (Progress)
	{
		Progress->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!IsInViewport())
	{
		AddToViewport(15);
	}
	World->GetTimerManager().SetTimer(HideTimer, this, &ThisClass::HideNow, 1.5f, false);
}

void UFPSRLReviveWidget::HideNow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	RemoveFromParent();
}

void UFPSRLReviveWidget::UpdateProgress()
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!Progress || !GameState)
	{
		return;
	}
	const double Duration = FMath::Max(EndTime - StartTime, 0.01);
	const double Alpha = (GameState->GetServerWorldTimeSeconds() - StartTime) / Duration;
	Progress->SetPercent(FMath::Clamp(static_cast<float>(Alpha), 0.f, 1.f));
}

void UFPSRLReviveWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::NativeDestruct();
}
