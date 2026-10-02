// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLChatWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/FPSRLPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Social/FPSRLChatFilter.h"
#include "Social/FPSRLChatSettings.h"
#include "Social/FPSRLChatSubsystem.h"
#include "Social/FPSRLProfanityFilter.h"
#include "Social/FPSRLUserSettings.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FPSRLChat"

namespace FPSRLChatUI
{
	const FLinearColor OwnName(1.f, 0.85f, 0.45f);		// the HUD's warm accent (as the catch-up prompt)
	const FLinearColor OtherName(0.55f, 0.85f, 1.f);
	const FLinearColor Body(0.95f, 0.95f, 0.95f);
	const FLinearColor Notice(1.f, 0.45f, 0.35f);
	const float Width = 500.f;
	const float OpenHeight = 240.f;
	const float NoticeSeconds = 3.f;
}

void UFPSRLChatWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	if (UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this))
	{
		AddedHandle = Chat->OnMessageAdded.AddUObject(this, &ThisClass::HandleMessageAdded);
		ResetHandle = Chat->OnHistoryReset.AddUObject(this, &ThisClass::HandleHistoryReset);
	}
	SettingsHandle = UFPSRLUserSettings::OnChanged.AddUObject(this, &ThisClass::Rebuild);	// filter toggled: redraw
	Rebuild();
	SetOpen(false);
}

void UFPSRLChatWidget::NativeDestruct()
{
	UFPSRLUserSettings::OnChanged.Remove(SettingsHandle);
	if (UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this))
	{
		Chat->OnMessageAdded.Remove(AddedHandle);
		Chat->OnHistoryReset.Remove(ResetHandle);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FadeTimer);
	}
	Super::NativeDestruct();
}

void UFPSRLChatWidget::BuildLayout()
{
	auto MakeText = [this](const TCHAR* Name, int32 Size, const FLinearColor& Colour)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		return Text;
	};

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	WidgetTree->RootWidget = Canvas;

	// Bottom left, above the health readout.
	USizeBox* WidthBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChatWidth"));
	WidthBox->SetWidthOverride(FPSRLChatUI::Width);
	if (UCanvasPanelSlot* BoxSlot = Canvas->AddChildToCanvas(WidthBox))
	{
		BoxSlot->SetAnchors(FAnchors(0.f, 1.f));
		BoxSlot->SetAlignment(FVector2D(0.f, 1.f));
		BoxSlot->SetPosition(FVector2D(40.f, -120.f));
		BoxSlot->SetAutoSize(true);
	}
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChatPanel"));
	Panel->SetPadding(FMargin(10.f, 8.f));
	WidthBox->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChatColumn"));
	Panel->SetContent(Column);

	HistoryBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChatHistoryBox"));
	Lines = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ChatLines"));
	Lines->SetAlwaysShowScrollbar(false);
	HistoryBox->SetContent(Lines);
	Column->AddChildToVerticalBox(HistoryBox);

	NoticeText = MakeText(TEXT("ChatNotice"), 13, FPSRLChatUI::Notice);
	NoticeText->SetAutoWrapText(true);
	NoticeText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(NoticeText)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	// Input row: "> [text box] 0/160".
	InputRow = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChatInputRow"));
	InputRow->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	InputRow->SetPadding(FMargin(8.f, 4.f));
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ChatInputLine"));
	InputRow->SetContent(Row);
	UTextBlock* Prompt = MakeText(TEXT("ChatPrompt"), 15, FPSRLChatUI::OwnName);
	Prompt->SetText(FText::FromString(TEXT(">")));
	if (UHorizontalBoxSlot* PromptSlot = Row->AddChildToHorizontalBox(Prompt))
	{
		PromptSlot->SetVerticalAlignment(VAlign_Center);
		PromptSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}
	InputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInput"));
	InputBox->SetHintText(LOCTEXT("Hint", "Type a message... (Enter to send, Esc to cancel)"));
	InputBox->SetClearKeyboardFocusOnCommit(false);
	{
		// Dark field, white text: the same look as the selection screens.
		FEditableTextBoxStyle Style = InputBox->WidgetStyle;
		Style.SetBackgroundColor(FSlateColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f)));
		Style.SetForegroundColor(FSlateColor(FLinearColor::White));
		Style.SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
		Style.TextStyle.Font.Size = 15;
		InputBox->WidgetStyle = Style;
	}
	InputBox->OnTextChanged.AddDynamic(this, &ThisClass::HandleTextChanged);
	InputBox->OnTextCommitted.AddDynamic(this, &ThisClass::HandleTextCommitted);
	if (UHorizontalBoxSlot* InputSlot = Row->AddChildToHorizontalBox(InputBox))
	{
		InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		InputSlot->SetVerticalAlignment(VAlign_Center);
	}
	Counter = MakeText(TEXT("ChatCounter"), 12, FLinearColor(0.7f, 0.7f, 0.7f));
	if (UHorizontalBoxSlot* CounterSlot = Row->AddChildToHorizontalBox(Counter))
	{
		CounterSlot->SetVerticalAlignment(VAlign_Center);
		CounterSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
	}
	Column->AddChildToVerticalBox(InputRow)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	HandleTextChanged(FText::GetEmpty());
}

void UFPSRLChatWidget::Rebuild()
{
	if (!Lines)
	{
		return;
	}
	Lines->ClearChildren();
	LineWidgets.Reset();
	if (const UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this))
	{
		for (const FFPSRLChatMessage& Message : Chat->GetClientHistory())
		{
			AddLine(Message, Message.LocalArrivalSeconds);
		}
	}
	RefreshLines();
}

void UFPSRLChatWidget::AddLine(const FFPSRLChatMessage& Message, double ArrivedAt)
{
	// "Name: message" as two plain text blocks (the name coloured, the message wrapping). Never rich text: no markup.
	const APlayerController* PC = GetOwningPlayer();
	const bool bMine = PC && PC->PlayerState && PC->PlayerState->GetPlayerId() == Message.SenderPlayerId;
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	UTextBlock* Body = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	for (UTextBlock* Text : { Name, Body })
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 15;
		Text->SetFont(Font);
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	}
	Name->SetText(FText::FromString(Message.DisplayName + TEXT(":")));
	Name->SetColorAndOpacity(FSlateColor(bMine ? FPSRLChatUI::OwnName : FPSRLChatUI::OtherName));
	// This player's own preference: profanity masked for display (the history keeps the original).
	Body->SetText(FText::FromString(UFPSRLUserSettings::Get()->bFilterProfanity ? FPSRLProfanity::Mask(Message.Text) : Message.Text));
	LastShownText = Body->GetText().ToString();
	Body->SetColorAndOpacity(FSlateColor(FPSRLChatUI::Body));
	Body->SetAutoWrapText(true);
	if (UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(Name))
	{
		NameSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	}
	Line->AddChildToHorizontalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Lines->AddChild(Line);

	FLine& Entry = LineWidgets.AddDefaulted_GetRef();
	Entry.Widget = Line;
	Entry.ArrivedAt = ArrivedAt;
	Entry.MessageId = Message.MessageId;

	// Same cap as the history.
	const int32 Max = UFPSRLChatSettings::Get()->MaxClientChatHistory;
	while (LineWidgets.Num() > Max)
	{
		if (UWidget* Old = LineWidgets[0].Widget.Get())
		{
			Old->RemoveFromParent();
		}
		LineWidgets.RemoveAt(0);
	}
}

void UFPSRLChatWidget::HandleMessageAdded(const FFPSRLChatMessage& Message)
{
	// History order can only change through a history sync, which rebuilds; live messages arrive in order.
	if (!LineWidgets.IsEmpty() && LineWidgets.Last().MessageId > Message.MessageId)
	{
		Rebuild();
		return;
	}
	AddLine(Message, Message.LocalArrivalSeconds);
	RefreshLines();
	Lines->ScrollToEnd();
	StartFadeTimer();
}

void UFPSRLChatWidget::HandleHistoryReset()
{
	Rebuild();
}

void UFPSRLChatWidget::RefreshLines()
{
	const UFPSRLChatSettings* Settings = UFPSRLChatSettings::Get();
	const double Now = FPlatformTime::Seconds();
	bool bAnimating = false;

	// Open: every line, full strength, on a dark panel. Compact: the newest lines, each fading after the delay.
	Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, bIsOpen ? 0.45f : 0.f));
	HistoryBox->ClearHeightOverride();
	if (bIsOpen)
	{
		HistoryBox->SetHeightOverride(FPSRLChatUI::OpenHeight);
	}
	const int32 FirstCompact = LineWidgets.Num() - Settings->MaxVisibleMessages;
	for (int32 Index = 0; Index < LineWidgets.Num(); ++Index)
	{
		UWidget* Line = LineWidgets[Index].Widget.Get();
		if (!Line)
		{
			continue;
		}
		float Alpha = 1.f;
		if (!bIsOpen)
		{
			const double Age = LineWidgets[Index].ArrivedAt > 0.0 ? Now - LineWidgets[Index].ArrivedAt : 1e9;	// join history: already faded
			Alpha = Index < FirstCompact ? 0.f
				: Age <= Settings->MessageFadeDelay ? 1.f
				: FMath::Clamp(1.f - static_cast<float>((Age - Settings->MessageFadeDelay) / Settings->MessageFadeDuration), 0.f, 1.f);
			bAnimating |= Alpha > 0.f;
		}
		Line->SetRenderOpacity(Alpha);
		Line->SetVisibility(Alpha > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	const bool bNotice = Now < NoticeUntil;
	NoticeText->SetVisibility(bNotice ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	bAnimating |= bNotice;

	if (!bAnimating && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(FadeTimer);
	}
}

void UFPSRLChatWidget::StartFadeTimer()
{
	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(FadeTimer))
	{
		World->GetTimerManager().SetTimer(FadeTimer, this, &ThisClass::RefreshLines, 0.1f, true);	// only while something fades
	}
}

void UFPSRLChatWidget::SetOpen(bool bOpen)
{
	bIsOpen = bOpen;
	if (InputRow)
	{
		InputRow->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Lines)
	{
		Lines->SetScrollBarVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// Closed, the chat never takes the mouse or keyboard.
	SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
	if (Panel)
	{
		RefreshLines();
		Lines->ScrollToEnd();
		if (!bOpen)
		{
			StartFadeTimer();	// lines that faded while open are hidden again; recent ones keep fading
		}
	}
}

void UFPSRLChatWidget::ShowNotice(const FText& Notice)
{
	if (NoticeText)
	{
		NoticeText->SetText(Notice);
		NoticeUntil = FPlatformTime::Seconds() + FPSRLChatUI::NoticeSeconds;
		RefreshLines();
		StartFadeTimer();
	}
}

void UFPSRLChatWidget::HandleTextChanged(const FText& Text)
{
	if (bUpdatingText || !InputBox)
	{
		return;
	}
	// Stop at the limit (the server checks it again on its own).
	const int32 Max = UFPSRLChatSettings::Get()->MaxMessageCharacters;
	FString Value = Text.ToString();
	if (Value.Len() > Max)
	{
		TGuardValue<bool> Guard(bUpdatingText, true);
		Value.LeftInline(Max);
		InputBox->SetText(FText::FromString(Value));
	}
	if (Counter)
	{
		Counter->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Value.Len(), Max)));
	}
}

void UFPSRLChatWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (!bIsOpen)
	{
		return;
	}
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Submit();
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		Cancel();
	}
}

FReply UFPSRLChatWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Esc or gamepad B closes the chat before the text box (or the pause key) sees it.
	const FKey Key = InKeyEvent.GetKey();
	if (bIsOpen && (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right))
	{
		Cancel();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UFPSRLChatWidget::Submit()
{
	AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(GetOwningPlayer());
	const FString Text = InputBox ? UFPSRLChatFilter::Sanitize(InputBox->GetText().ToString()) : FString();
	if (PC && !Text.IsEmpty())
	{
		PC->ServerSendChatMessage(Text.Left(UFPSRLChatSettings::Get()->MaxMessageCharacters));
	}
	if (InputBox)
	{
		InputBox->SetText(FText::GetEmpty());
	}
	if (PC && (Text.IsEmpty() || !UFPSRLChatSettings::Get()->bKeepChatOpenAfterSend))
	{
		PC->CloseChat();	// Enter on an empty line also just closes
	}
}

void UFPSRLChatWidget::Cancel()
{
	// The draft stays in the box for next time; nothing is sent.
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(GetOwningPlayer()))
	{
		PC->CloseChat();
	}
}

void UFPSRLChatWidget::SetDraftForTest(const FString& Text)
{
	if (InputBox)
	{
		InputBox->SetText(FText::FromString(Text));
		HandleTextChanged(InputBox->GetText());
	}
}

FString UFPSRLChatWidget::DescribeForTest() const
{
	int32 Shown = 0;
	FString Last;
	for (const FLine& Line : LineWidgets)
	{
		const UWidget* Widget = Line.Widget.Get();
		if (Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetRenderOpacity() > 0.f)
		{
			++Shown;
		}
	}
	if (!LineWidgets.IsEmpty())
	{
		Last = FString::Printf(TEXT("#%d"), LineWidgets.Last().MessageId);
	}
	return FString::Printf(TEXT("%s | %d line(s), %d shown, last %s '%s' | input '%s' | notice %s"),
		bIsOpen ? TEXT("open") : TEXT("closed"), LineWidgets.Num(), Shown, *Last, *LastShownText,
		InputBox ? *InputBox->GetText().ToString() : TEXT("-"),
		NoticeText && NoticeText->GetVisibility() != ESlateVisibility::Collapsed ? *NoticeText->GetText().ToString() : TEXT("-"));
}

#undef LOCTEXT_NAMESPACE
