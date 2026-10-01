// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLCatchUpWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "TimerManager.h"

void UFPSRLCatchUpWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);	// never takes input
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// Upper middle: a dark panel with the message and the keys.
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	WidgetTree->RootWidget = Column;
	UVerticalBox* TopSpacer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TopSpacer"));
	FSlateChildSize TopShare(ESlateSizeRule::Fill);
	TopShare.Value = 0.18f;
	Column->AddChildToVerticalBox(TopSpacer)->SetSize(TopShare);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel->SetPadding(FMargin(22.f, 12.f));
	UVerticalBoxSlot* PanelSlot = Column->AddChildToVerticalBox(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);

	UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Lines"));
	Panel->SetContent(Lines);

	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 20;
	Title->SetFont(TitleFont);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.45f)));
	Title->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Lines->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	Keys = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Keys"));
	FSlateFontInfo KeysFont = Keys->GetFont();
	KeysFont.Size = 16;
	Keys->SetFont(KeysFont);
	Keys->SetJustification(ETextJustify::Center);
	Lines->AddChildToVerticalBox(Keys)->SetHorizontalAlignment(HAlign_Center);

	UVerticalBox* BottomSpacer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BottomSpacer"));
	Column->AddChildToVerticalBox(BottomSpacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UFPSRLCatchUpWidget::ShowOffer(const FText& Message, const FText& RoomName)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	if (Title)
	{
		Title->SetText(Message);
	}
	if (Keys)
	{
		Keys->SetText(FText::Format(NSLOCTEXT("FPSRL", "CatchUpKeys", "Press [G] to teleport to them"), RoomName));
		Keys->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (!IsInViewport())
	{
		AddToViewport(14);
	}
}

void UFPSRLCatchUpWidget::HideOffer(const FText& Message)
{
	UWorld* World = GetWorld();
	if (Message.IsEmpty() || !World)
	{
		HideNow();
		return;
	}
	if (Title)
	{
		Title->SetText(Message);
	}
	if (Keys)
	{
		Keys->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!IsInViewport())
	{
		AddToViewport(14);
	}
	World->GetTimerManager().SetTimer(HideTimer, this, &ThisClass::HideNow, 2.5f, false);
}

FText UFPSRLCatchUpWidget::GetShownTitle() const
{
	return Title && IsInViewport() ? Title->GetText() : FText::GetEmpty();
}

void UFPSRLCatchUpWidget::HideNow()
{
	RemoveFromParent();
}

void UFPSRLCatchUpWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}
	Super::NativeDestruct();
}
