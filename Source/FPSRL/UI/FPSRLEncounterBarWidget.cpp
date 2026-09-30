// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLEncounterBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace FPSRLEncounterBarStyle
{
	const FLinearColor Miniboss(1.f, 0.55f, 0.1f);
	const FLinearColor FinalLevelBoss(0.9f, 0.08f, 0.08f);
}

void UFPSRLEncounterBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UFPSRLEncounterBarWidget::BuildDefaultLayout()
{
	// Top-centre: the name over a wide bar.
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	WidgetTree->RootWidget = Column;

	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	FSlateFontInfo Font = Title->GetFont();
	Font.Size = 24;
	Title->SetFont(Font);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 30.f, 0.f, 4.f));
	}

	USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BarSize->SetWidthOverride(700.f);
	BarSize->SetHeightOverride(22.f);
	HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	HealthBar->SetPercent(1.f);
	BarSize->SetContent(HealthBar);
	if (UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(BarSize))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Center);
	}
}

void UFPSRLEncounterBarWidget::SetTarget(UFPSRLHealthComponent* InHealth, const FText& Label, EFPSRLEncounterKind Kind)
{
	if (Health.Get() != InHealth)
	{
		ClearTarget();
		Health = InHealth;
		if (InHealth)
		{
			InHealth->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
		}
	}
	if (Title)
	{
		Title->SetText(Label);
	}
	if (HealthBar)
	{
		HealthBar->SetFillColorAndOpacity(Kind == EFPSRLEncounterKind::FinalLevelBoss ? FPSRLEncounterBarStyle::FinalLevelBoss : FPSRLEncounterBarStyle::Miniboss);
	}
	if (InHealth)
	{
		HandleHealthChanged(InHealth->GetCurrentHealth(), InHealth->GetMaxHealth());
	}
}

void UFPSRLEncounterBarWidget::ClearTarget()
{
	if (UFPSRLHealthComponent* Old = Health.Get())
	{
		Old->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
	}
	Health.Reset();
}

void UFPSRLEncounterBarWidget::HandleHealthChanged(double CurrentHealth, double MaxHealth)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(MaxHealth > 0.0 ? static_cast<float>(FMath::Clamp(CurrentHealth / MaxHealth, 0.0, 1.0)) : 0.f);
	}
}

void UFPSRLEncounterBarWidget::NativeDestruct()
{
	ClearTarget();
	Super::NativeDestruct();
}
