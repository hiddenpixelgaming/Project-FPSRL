// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLServerBrowserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/FPSRLSessionSubsystem.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "FPSRLServerBrowser"

namespace FPSRLBrowserStyle
{
	static const FLinearColor Text(0.9f, 0.9f, 0.9f);
	static const FLinearColor Dim(0.6f, 0.6f, 0.6f);
	static const FLinearColor Waiting(0.35f, 0.9f, 0.45f);
	static const FLinearColor InRun(1.f, 0.45f, 0.3f);
	static const FLinearColor Error(1.f, 0.45f, 0.3f);
	static constexpr float PlayersWidth = 110.f;
	static constexpr float StatusWidth = 220.f;
	static constexpr float JoinWidth = 120.f;
}

void UFPSRLServerBrowserRowHandler::HandleClicked()
{
	if (UFPSRLServerBrowserWidget* Widget = Browser.Get())
	{
		Widget->JoinRow(ResultIndex);
	}
}

void UFPSRLServerBrowserWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	RefreshButton->OnClicked.AddDynamic(this, &ThisClass::HandleRefreshClicked);
	BackButton->OnClicked.AddDynamic(this, &ThisClass::HandleBack);
}

void UFPSRLServerBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UFPSRLSessionSubsystem* Sessions = GetSessions())
	{
		SessionsFoundHandle = Sessions->OnSessionsFound.AddUObject(this, &ThisClass::HandleSessionsFound);
		JoinFailedHandle = Sessions->OnJoinFailed.AddUObject(this, &ThisClass::HandleJoinFailed);
	}
	HandleRefresh();
}

void UFPSRLServerBrowserWidget::NativeDestruct()
{
	if (UFPSRLSessionSubsystem* Sessions = GetSessions())
	{
		Sessions->OnSessionsFound.Remove(SessionsFoundHandle);
		Sessions->OnJoinFailed.Remove(JoinFailedHandle);
	}
	Super::NativeDestruct();
}

UFPSRLSessionSubsystem* UFPSRLServerBrowserWidget::GetSessions() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UFPSRLSessionSubsystem>() : nullptr;
}

// --- Layout ----------------------------------------------------------------------------------------------------------

UTextBlock* UFPSRLServerBrowserWidget::MakeText(const FText& Text, float FontSize, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(Text);
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = FontSize;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	return Block;
}

UButton* UFPSRLServerBrowserWidget::MakeButton(const FName& Name, const FText& Label, float FontSize)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetContent(MakeText(Label, FontSize, FLinearColor::Black));
	return Button;
}

/** Fixed-width cell so the columns line up between the header and every row. */
static UWidget* FixedWidth(UWidgetTree* Tree, UWidget* Content, float Width)
{
	USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetWidthOverride(Width);
	Box->SetContent(Content);
	return Box;
}

static void AddCell(UHorizontalBox* Row, UWidget* Cell, bool bFill)
{
	if (UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Cell))
	{
		CellSlot->SetVerticalAlignment(VAlign_Center);
		CellSlot->SetPadding(FMargin(8.f, 4.f));
		if (bFill)
		{
			CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
}

void UFPSRLServerBrowserWidget::BuildLayout()
{
	using namespace FPSRLBrowserStyle;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.07f, 0.95f));
	Panel->SetPadding(FMargin(32.f));
	Backdrop->SetContent(Panel);

	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	PanelSize->SetWidthOverride(960.f);
	Panel->SetContent(PanelSize);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	PanelSize->SetContent(Column);

	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(MakeText(LOCTEXT("Title", "JOIN A GAME"), 40, Text)))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	}

	MessageText = MakeText(FText::GetEmpty(), 18, Dim);
	MessageText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* MessageSlot = Column->AddChildToVerticalBox(MessageText))
	{
		MessageSlot->SetHorizontalAlignment(HAlign_Center);
		MessageSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 16.f));
	}

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Header"));
	AddCell(Header, MakeText(LOCTEXT("HostColumn", "HOST"), 16, Dim), true);
	AddCell(Header, FixedWidth(WidgetTree, MakeText(LOCTEXT("PlayersColumn", "PLAYERS"), 16, Dim), PlayersWidth), false);
	AddCell(Header, FixedWidth(WidgetTree, MakeText(LOCTEXT("StatusColumn", "STATUS"), 16, Dim), StatusWidth), false);
	AddCell(Header, FixedWidth(WidgetTree, MakeText(FText::GetEmpty(), 16, Dim), JoinWidth), false);
	Column->AddChildToVerticalBox(Header);

	USizeBox* ListSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ListSize->SetHeightOverride(420.f);
	RowList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RowList"));
	ListSize->SetContent(RowList);
	Column->AddChildToVerticalBox(ListSize);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	RefreshButton = MakeButton(TEXT("RefreshButton"), LOCTEXT("Refresh", "Refresh"), 22);
	BackButton = MakeButton(TEXT("BackButton"), LOCTEXT("Back", "Back"), 22);
	AddCell(Buttons, FixedWidth(WidgetTree, RefreshButton, 200.f), false);
	AddCell(Buttons, FixedWidth(WidgetTree, BackButton, 200.f), false);
	if (UVerticalBoxSlot* ButtonsSlot = Column->AddChildToVerticalBox(Buttons))
	{
		ButtonsSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonsSlot->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
	}
}

// --- Behaviour -------------------------------------------------------------------------------------------------------

void UFPSRLServerBrowserWidget::ShowMessage(const FText& Message, bool bIsError)
{
	bErrorShown = bIsError;
	MessageText->SetText(Message);
	MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FPSRLBrowserStyle::Error : FPSRLBrowserStyle::Dim));
}

void UFPSRLServerBrowserWidget::HandleRefresh()
{
	UFPSRLSessionSubsystem* Sessions = GetSessions();
	if (!Sessions || Sessions->IsSearching())
	{
		return;
	}
	RowList->ClearChildren();
	RowHandlers.Reset();
	RefreshButton->SetIsEnabled(false);
	if (!bErrorShown)
	{
		ShowMessage(LOCTEXT("Searching", "Searching for games..."), false);
	}
	Sessions->FindSessions();
}

void UFPSRLServerBrowserWidget::HandleRefreshClicked()
{
	bErrorShown = false;
	HandleRefresh();
}

void UFPSRLServerBrowserWidget::HandleSessionsFound(bool bSuccess)
{
	RefreshButton->SetIsEnabled(true);
	if (!bSuccess)
	{
		ShowMessage(LOCTEXT("SearchFailed", "Session search failed. Check your Steam connection."), true);
		return;
	}
	RebuildRows();
	const UFPSRLSessionSubsystem* Sessions = GetSessions();
	if (bErrorShown)
	{
		return;
	}
	ShowMessage(Sessions && Sessions->GetRows().Num() > 0
		? LOCTEXT("PickGame", "Pick a game to join.")
		: LOCTEXT("NoGames", "No games found. Ask your host to create one, then Refresh."), false);
}

void UFPSRLServerBrowserWidget::RebuildRows()
{
	using namespace FPSRLBrowserStyle;

	RowList->ClearChildren();
	RowHandlers.Reset();
	const UFPSRLSessionSubsystem* Sessions = GetSessions();
	if (!Sessions)
	{
		return;
	}
	for (const FFPSRLSessionRow& Session : Sessions->GetRows())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		AddCell(Row, MakeText(FText::FromString(Session.HostName), 20, Text), true);
		AddCell(Row, FixedWidth(WidgetTree, MakeText(FText::Format(LOCTEXT("Players", "{0} / {1}"), Session.Players, Session.MaxPlayers), 20, Text), PlayersWidth), false);
		AddCell(Row, FixedWidth(WidgetTree, MakeText(Session.bRunInProgress ? LOCTEXT("InRun", "Run in progress") : LOCTEXT("Waiting", "Waiting in Lobby"), 20,
			Session.bRunInProgress ? InRun : Waiting), StatusWidth), false);

		UButton* Join = MakeButton(NAME_None, LOCTEXT("Join", "Join"), 20);
		UFPSRLServerBrowserRowHandler* Handler = NewObject<UFPSRLServerBrowserRowHandler>(this);
		Handler->Browser = this;
		Handler->ResultIndex = Session.ResultIndex;
		Join->OnClicked.AddDynamic(Handler, &UFPSRLServerBrowserRowHandler::HandleClicked);
		RowHandlers.Add(Handler);
		AddCell(Row, FixedWidth(WidgetTree, Join, JoinWidth), false);

		RowList->AddChild(Row);
	}
}

void UFPSRLServerBrowserWidget::JoinRow(int32 ResultIndex)
{
	UFPSRLSessionSubsystem* Sessions = GetSessions();
	if (!Sessions || Sessions->IsJoining())
	{
		return;
	}
	ShowMessage(LOCTEXT("Joining", "Joining..."), false);
	Sessions->JoinSession(ResultIndex, GetOwningPlayer());	// failure comes back through HandleJoinFailed
}

void UFPSRLServerBrowserWidget::HandleJoinFailed(const FText& Reason)
{
	if (UFPSRLSessionSubsystem* Sessions = GetSessions())
	{
		Sessions->ConsumePendingJoinError();	// shown here; don't show it again on the next Menu load
	}
	ShowMessage(Reason, true);
}

void UFPSRLServerBrowserWidget::HandleBack()
{
	RemoveFromParent();
}

FReply UFPSRLServerBrowserWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		HandleBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
