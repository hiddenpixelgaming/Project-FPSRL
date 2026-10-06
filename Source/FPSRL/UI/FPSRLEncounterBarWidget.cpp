// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLEncounterBarWidget.h"
#include "AI/FPSRLShieldEncounterComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace FPSRLEncounterBarStyle
{
	const FLinearColor Miniboss(1.f, 0.55f, 0.1f);
	const FLinearColor FinalLevelBoss(0.9f, 0.08f, 0.08f);
	const FLinearColor Shielded(0.35f, 0.55f, 0.65f);		// health while the shield is up (reduced damage)
	const FLinearColor Marker(1.f, 1.f, 1.f, 0.95f);			// a threshold still to come
	const FLinearColor MarkerPassed(1.f, 1.f, 1.f, 0.25f);
	const FLinearColor ShieldUp(0.15f, 0.75f, 1.f);
	const FLinearColor ShieldDown(0.12f, 0.12f, 0.14f);
	const FLinearColor Disruption(0.2f, 0.9f, 1.f);
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
	// Top-centre: the name over [SHIELD UP/DOWN][ health bar with its threshold markers ], the disruption bar under it.
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	WidgetTree->RootWidget = Column;

	auto MakeText = [this](const TCHAR* Name, int32 Size)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		return Text;
	};

	Title = MakeText(TEXT("Title"), 24);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.f, 30.f, 0.f, 4.f));
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	for (int32 Index = 0; Index < 1; ++Index)
	{
		UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("Shield%d"), Index + 1));
		Box->SetPadding(FMargin(10.f, 1.f));
		Box->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Text = MakeText(*FString::Printf(TEXT("ShieldText%d"), Index + 1), 11);
		Box->SetContent(Text);
		if (UHorizontalBoxSlot* BoxSlot = Row->AddChildToHorizontalBox(Box))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		}
		Box->SetVisibility(ESlateVisibility::Collapsed);
		ShieldBoxes.Add(Box);
		ShieldTexts.Add(Text);
	}
	USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BarSize->SetWidthOverride(700.f);
	BarSize->SetHeightOverride(22.f);
	HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	HealthBar->SetPercent(1.f);
	UOverlay* BarOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("BarOverlay"));
	if (UOverlaySlot* BarSlot = BarOverlay->AddChildToOverlay(HealthBar))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetVerticalAlignment(VAlign_Fill);
	}
	Markers = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Markers"));
	if (UOverlaySlot* MarkersSlot = BarOverlay->AddChildToOverlay(Markers))
	{
		MarkersSlot->SetHorizontalAlignment(HAlign_Fill);
		MarkersSlot->SetVerticalAlignment(VAlign_Fill);
	}
	BarSize->SetContent(BarOverlay);
	Row->AddChildToHorizontalBox(BarSize);
	if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Row))
	{
		RowSlot->SetHorizontalAlignment(HAlign_Center);
	}

	DisruptionText = MakeText(TEXT("DisruptionText"), 16);
	DisruptionText->SetColorAndOpacity(FSlateColor(FPSRLEncounterBarStyle::Disruption));
	if (UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(DisruptionText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 2.f));
	}
	USizeBox* DisruptionSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	DisruptionSize->SetWidthOverride(420.f);
	DisruptionSize->SetHeightOverride(14.f);
	DisruptionBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("DisruptionBar"));
	DisruptionBar->SetFillColorAndOpacity(FPSRLEncounterBarStyle::Disruption);
	DisruptionSize->SetContent(DisruptionBar);
	if (UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(DisruptionSize))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Center);
	}
	DisruptionText->SetVisibility(ESlateVisibility::Collapsed);
	DisruptionBar->SetVisibility(ESlateVisibility::Collapsed);
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
			if (UFPSRLShieldEncounterComponent* ShieldComponent = InHealth->GetOwner()->FindComponentByClass<UFPSRLShieldEncounterComponent>())
			{
				Shields = ShieldComponent;
				ShieldHandle = ShieldComponent->OnStateChanged.AddUObject(this, &ThisClass::RefreshShields);
			}
		}
	}
	if (Title)
	{
		Title->SetText(Label);
	}
	HealthColor = Kind == EFPSRLEncounterKind::FinalLevelBoss ? FPSRLEncounterBarStyle::FinalLevelBoss : FPSRLEncounterBarStyle::Miniboss;
	if (InHealth)
	{
		HandleHealthChanged(InHealth->GetCurrentHealth(), InHealth->GetMaxHealth());
	}
	RefreshShields();
}

void UFPSRLEncounterBarWidget::ClearTarget()
{
	if (UFPSRLHealthComponent* Old = Health.Get())
	{
		Old->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
	}
	if (UFPSRLShieldEncounterComponent* OldShields = Shields.Get())
	{
		OldShields->OnStateChanged.Remove(ShieldHandle);
	}
	Health.Reset();
	Shields.Reset();
}

void UFPSRLEncounterBarWidget::HandleHealthChanged(double CurrentHealth, double MaxHealth)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(MaxHealth > 0.0 ? static_cast<float>(FMath::Clamp(CurrentHealth / MaxHealth, 0.0, 1.0)) : 0.f);
	}
}

void UFPSRLEncounterBarWidget::RefreshShields()
{
	const UFPSRLShieldEncounterComponent* ShieldComponent = Shields.Get();
	const int32 Layers = ShieldComponent ? ShieldComponent->GetShieldLayers() : 0;
	const int32 Remaining = ShieldComponent ? ShieldComponent->GetShieldsRemaining() : 0;
	const bool bUp = ShieldComponent && Layers > 0 && ShieldComponent->IsShieldUp();
	for (int32 Index = 0; Index < ShieldBoxes.Num(); ++Index)
	{
		ShieldBoxes[Index]->SetVisibility(Layers > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		ShieldBoxes[Index]->SetBrushColor(bUp ? FPSRLEncounterBarStyle::ShieldUp : FPSRLEncounterBarStyle::ShieldDown);
		ShieldTexts[Index]->SetText(FText::FromString(bUp ? TEXT("SHIELD UP") : TEXT("SHIELD DOWN")));
	}
	if (HealthBar)
	{
		HealthBar->SetFillColorAndOpacity(bUp ? FPSRLEncounterBarStyle::Shielded : HealthColor);
	}
	// The threshold markers on the health bar (75 / 50 / 25 %): the ones already reached fade.
	const TArray<float> Wanted = ShieldComponent ? ShieldComponent->GetThresholds() : TArray<float>();
	if (Markers && Wanted != MarkersShown)
	{
		MarkersShown = Wanted;
		Markers->ClearChildren();
		MarkerBorders.Reset();
		TArray<float> Ascending = Wanted;
		Ascending.Sort();
		float Previous = 0.f;
		for (const float Fraction : Ascending)
		{
			USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
			if (UHorizontalBoxSlot* GapSlot = Markers->AddChildToHorizontalBox(Gap))
			{
				GapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				GapSlot->Size.Value = FMath::Max(0.001f, Fraction - Previous);
			}
			UBorder* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Line->SetBrushColor(FPSRLEncounterBarStyle::Marker);
			USizeBox* LineSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			LineSize->SetWidthOverride(3.f);
			LineSize->SetContent(Line);
			Markers->AddChildToHorizontalBox(LineSize);
			MarkerBorders.Add(Fraction, Line);
			Previous = Fraction;
		}
		USpacer* Rest = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		if (UHorizontalBoxSlot* RestSlot = Markers->AddChildToHorizontalBox(Rest))
		{
			RestSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RestSlot->Size.Value = FMath::Max(0.001f, 1.f - Previous);
		}
	}
	for (int32 Index = 0; Index < Wanted.Num(); ++Index)
	{
		if (const TObjectPtr<UBorder>* Line = MarkerBorders.Find(Wanted[Index]))
		{
			(*Line)->SetBrushColor(Index < Layers - Remaining ? FPSRLEncounterBarStyle::MarkerPassed : FPSRLEncounterBarStyle::Marker);
		}
	}
	// The disruption bar while the platform mechanic runs.
	const EFPSRLShieldPhase Phase = ShieldComponent ? ShieldComponent->GetPhase() : EFPSRLShieldPhase::Idle;
	const bool bMechanic = Phase == EFPSRLShieldPhase::Throwing || Phase == EFPSRLShieldPhase::Charging || Phase == EFPSRLShieldPhase::AwaitingPlayers
		|| Phase == EFPSRLShieldPhase::Disrupting;
	const bool bForced = ShieldComponent && ShieldComponent->IsForcedPull();
	if (DisruptionText && DisruptionBar)
	{
		DisruptionText->SetVisibility(bMechanic ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		DisruptionBar->SetVisibility(bMechanic && !bForced && Phase != EFPSRLShieldPhase::Throwing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bMechanic)
		{
			const FString Status = Phase == EFPSRLShieldPhase::Throwing ? FString(TEXT("MINES  -  a pull is coming"))
				: bForced ? FString(TEXT("SHOCKWAVE  -  jump it or get clear"))
				: Phase == EFPSRLShieldPhase::Disrupting
				? FString::Printf(TEXT("SHIELD DISRUPTION  %.1f s"), ShieldComponent->GetDisruptionSecondsLeft())
				: Phase == EFPSRLShieldPhase::Charging ? FString(TEXT("SHIELD DISRUPTION  -  get to a lit platform"))
				: FString::Printf(TEXT("SHIELD DISRUPTION  paused  -  %d player(s) needed on the lit platforms"), ShieldComponent->GetRequiredCount());
			DisruptionText->SetText(FText::FromString(Status));
			DisruptionBar->SetPercent(ShieldComponent->GetDisruptionFraction());
		}
	}
}

FString UFPSRLEncounterBarWidget::Describe() const
{
	TArray<FString> Parts;
	for (int32 Index = 0; Index < ShieldTexts.Num(); ++Index)
	{
		if (ShieldBoxes[Index]->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Parts.Add(ShieldTexts[Index]->GetText().ToString());
		}
	}
	Parts.Add(FString::Printf(TEXT("HP %.0f%%"), HealthBar ? HealthBar->GetPercent() * 100.f : 0.f));
	if (!MarkersShown.IsEmpty())
	{
		TArray<FString> Marks;
		for (const float Fraction : MarkersShown)
		{
			Marks.Add(FString::Printf(TEXT("%.0f"), Fraction * 100.f));
		}
		Parts.Add(TEXT("markers ") + FString::Join(Marks, TEXT("/")));
	}
	if (DisruptionText && DisruptionText->GetVisibility() != ESlateVisibility::Collapsed)
	{
		Parts.Add(FString::Printf(TEXT("%s (%.0f%%)"), *DisruptionText->GetText().ToString(), DisruptionBar->GetPercent() * 100.f));
	}
	return FString::Join(Parts, TEXT(" | "));
}

void UFPSRLEncounterBarWidget::NativeDestruct()
{
	ClearTarget();
	Super::NativeDestruct();
}
