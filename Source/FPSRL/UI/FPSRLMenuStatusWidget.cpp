// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLMenuStatusWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UFPSRLMenuStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Bottom-centre line of text.
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		WidgetTree->RootWidget = Column;
		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
		FSlateFontInfo Font = StatusText->GetFont();
		Font.Size = 22;
		StatusText->SetFont(Font);
		StatusText->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(StatusText))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Bottom);
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 60.f));
		}
	}
}

void UFPSRLMenuStatusWidget::SetStatus(const FText& Message, bool bError)
{
	if (StatusText)
	{
		StatusText->SetText(Message);
		StatusText->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor::White));
	}
}
