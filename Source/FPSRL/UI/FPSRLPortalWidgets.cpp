// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLPortalWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/FPSRLRunSettings.h"
#include "GameFramework/GameStateBase.h"
#include "Rooms/FPSRLExitPortal.h"
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
		Text->SetJustification(ETextJustify::Center);
		return Text;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FName& Name, const FText& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = MakeText(Tree, NAME_None, 20, FLinearColor::Black);
		Text->SetText(Label);
		Button->SetContent(Text);
		return Button;
	}
}

// --- Text ------------------------------------------------------------------------------------------------------------

FText FPSRLPortalText::GetQuestion(const AFPSRLExitPortal* Portal)
{
	switch (Portal ? Portal->Destination : EPortalDestination::RunComplete)
	{
	case EPortalDestination::NextDepth:	return NSLOCTEXT("FPSRL", "PortalNextDepth", "Continue to the next Depth?");
	case EPortalDestination::NextArea:	return NSLOCTEXT("FPSRL", "PortalNextArea", "Leave for the next Area?");
	case EPortalDestination::FinalBoss:	return NSLOCTEXT("FPSRL", "PortalFinalBoss", "Continue to the Final Boss?");
	default:							return NSLOCTEXT("FPSRL", "PortalRunComplete", "Leave the run and return to the Lobby?");
	}
}

FText FPSRLPortalText::GetTracker(const AFPSRLExitPortal* Portal)
{
	if (!Portal)
	{
		return FText::GetEmpty();
	}
	const FText Ready = FText::Format(NSLOCTEXT("FPSRL", "PortalReady", "{0} / {1} ready to continue"), Portal->ContinueVotes.Num(), Portal->LivingPlayers);
	const AGameStateBase* GameState = Portal->GetWorld() ? Portal->GetWorld()->GetGameState() : nullptr;
	if (Portal->CountdownEndTime > 0.0 && GameState)
	{
		const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Portal->CountdownEndTime - GameState->GetServerWorldTimeSeconds()));
		return FText::Format(NSLOCTEXT("FPSRL", "PortalCountdown", "{0} - everyone moves in {1}s"), Ready, Seconds);
	}
	if (Portal->LivingPlayers > 1)
	{
		const int32 Needed = FMath::CeilToInt(Portal->LivingPlayers * UFPSRLRunSettings::Get().PortalVoteThreshold);
		return FText::Format(NSLOCTEXT("FPSRL", "PortalNeeded", "{0} - {1} start the countdown, all move at once"), Ready, Needed);
	}
	return Ready;
}

// --- Menu ------------------------------------------------------------------------------------------------------------

void UFPSRLPortalMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddDynamic(this, &ThisClass::HandleContinue);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &ThisClass::HandleCancel);
	}
}

void UFPSRLPortalMenuWidget::BuildDefaultLayout()
{
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Backdrop->SetContent(Column);

	auto Add = [Column](UWidget* Child, float VerticalPadding)
	{
		UVerticalBoxSlot* ChildSlot = Column->AddChildToVerticalBox(Child);
		ChildSlot->SetPadding(FMargin(0.f, VerticalPadding));
		ChildSlot->SetHorizontalAlignment(HAlign_Center);
	};

	Title = MakeText(WidgetTree, TEXT("Title"), 36, FLinearColor::White);
	Title->SetText(NSLOCTEXT("FPSRL", "PortalTitle", "EXIT PORTAL"));
	Add(Title, 8.f);

	Body = MakeText(WidgetTree, TEXT("Body"), 22, FLinearColor::White);
	Add(Body, 6.f);

	Tracker = MakeText(WidgetTree, TEXT("Tracker"), 18, FLinearColor(1.f, 0.85f, 0.3f));
	Add(Tracker, 6.f);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	ContinueButton = MakeButton(WidgetTree, TEXT("ContinueButton"), NSLOCTEXT("FPSRL", "PortalContinue", "Continue"));
	CancelButton = MakeButton(WidgetTree, TEXT("CancelButton"), NSLOCTEXT("FPSRL", "PortalCancel", "Cancel"));
	Buttons->AddChildToHorizontalBox(ContinueButton)->SetPadding(FMargin(12.f, 0.f));
	Buttons->AddChildToHorizontalBox(CancelButton)->SetPadding(FMargin(12.f, 0.f));
	Add(Buttons, 16.f);
}

void UFPSRLPortalMenuWidget::ShowPortal(const AFPSRLExitPortal* Portal, bool bLocalPlayerContinuing)
{
	if (Body)
	{
		Body->SetText(bLocalPlayerContinuing
			? NSLOCTEXT("FPSRL", "PortalWaiting", "You are ready. Waiting for the others - Cancel to stay.")
			: FPSRLPortalText::GetQuestion(Portal));
	}
	if (Tracker)
	{
		Tracker->SetText(FPSRLPortalText::GetTracker(Portal));
	}
	if (ContinueButton)
	{
		ContinueButton->SetIsEnabled(!bLocalPlayerContinuing);
	}
}

void UFPSRLPortalMenuWidget::HandleContinue() { OnContinue.ExecuteIfBound(); }
void UFPSRLPortalMenuWidget::HandleCancel() { OnCancel.ExecuteIfBound(); }

// --- HUD tracker -----------------------------------------------------------------------------------------------------

void UFPSRLPortalStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);	// never takes input
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		WidgetTree->RootWidget = Column;

		StatusText = MakeText(WidgetTree, TEXT("StatusText"), 20, FLinearColor(1.f, 0.85f, 0.3f));
		StatusText->SetShadowOffset(FVector2D(1.f, 1.f));
		UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(StatusText);
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(0.f, 90.f, 0.f, 0.f));	// below the top edge
	}
}

void UFPSRLPortalStatusWidget::ShowPortal(const AFPSRLExitPortal* InPortal)
{
	Portal = InPortal;
	UpdateText();
	if (UWorld* World = GetWorld())
	{
		if (InPortal && InPortal->CountdownEndTime > 0.0)
		{
			World->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::UpdateText, 0.25f, true);
		}
		else
		{
			World->GetTimerManager().ClearTimer(RefreshTimer);
		}
	}
}

void UFPSRLPortalStatusWidget::UpdateText()
{
	if (StatusText)
	{
		StatusText->SetText(FText::Format(NSLOCTEXT("FPSRL", "PortalStatus", "EXIT PORTAL: {0}"), FPSRLPortalText::GetTracker(Portal.Get())));
	}
}

void UFPSRLPortalStatusWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}
