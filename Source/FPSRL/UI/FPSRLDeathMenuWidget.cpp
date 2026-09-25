// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLDeathMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UTextBlock* MakeDeathText(UWidgetTree* Tree, const FName& Name, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		return Text;
	}

	UButton* MakeDeathButton(UWidgetTree* Tree, const FName& Name, const FText& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = MakeDeathText(Tree, NAME_None, 20, FLinearColor::Black);
		Text->SetText(Label);
		Button->SetContent(Text);
		return Button;
	}
}

void UFPSRLDeathMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (ReturnButton)
	{
		ReturnButton->OnClicked.AddDynamic(this, &ThisClass::HandleReturn);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &ThisClass::HandleQuit);
	}
}

void UFPSRLDeathMenuWidget::BuildDefaultLayout()
{
	// Transparent backdrop: the camera fade underneath is already black.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Backdrop->SetContent(Column);

	Title = MakeDeathText(WidgetTree, TEXT("Title"), 48, FLinearColor(0.85f, 0.1f, 0.1f));
	Title->SetText(NSLOCTEXT("FPSRL", "DeathTitle", "YOU DIED"));
	UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	TitleSlot->SetPadding(FMargin(0.f, 8.f));

	Status = MakeDeathText(WidgetTree, TEXT("Status"), 18, FLinearColor::White);
	UVerticalBoxSlot* StatusSlot = Column->AddChildToVerticalBox(Status);
	StatusSlot->SetHorizontalAlignment(HAlign_Center);
	StatusSlot->SetPadding(FMargin(0.f, 6.f));

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	ReturnButton = MakeDeathButton(WidgetTree, TEXT("ReturnButton"), NSLOCTEXT("FPSRL", "DeathReturn", "Return to Lobby"));
	QuitButton = MakeDeathButton(WidgetTree, TEXT("QuitButton"), NSLOCTEXT("FPSRL", "DeathQuit", "Quit Game"));
	Buttons->AddChildToHorizontalBox(ReturnButton)->SetPadding(FMargin(12.f, 0.f));
	Buttons->AddChildToHorizontalBox(QuitButton)->SetPadding(FMargin(12.f, 0.f));
	UVerticalBoxSlot* ButtonsSlot = Column->AddChildToVerticalBox(Buttons);
	ButtonsSlot->SetHorizontalAlignment(HAlign_Center);
	ButtonsSlot->SetPadding(FMargin(0.f, 18.f));
}

void UFPSRLDeathMenuWidget::SetCanReturn(bool bCanReturn)
{
	if (ReturnButton)
	{
		ReturnButton->SetIsEnabled(bCanReturn);
	}
	if (Status)
	{
		Status->SetText(bCanReturn
			? NSLOCTEXT("FPSRL", "DeathRunOver", "The run is over.")
			: NSLOCTEXT("FPSRL", "DeathTeamAlive", "Your team fights on. Return to Lobby opens if they fall too."));
	}
}

void UFPSRLDeathMenuWidget::HandleReturn() { OnReturnToLobby.ExecuteIfBound(); }
void UFPSRLDeathMenuWidget::HandleQuit() { OnQuitGame.ExecuteIfBound(); }
