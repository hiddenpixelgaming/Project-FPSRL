// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Social/FPSRLChatTypes.h"
#include "Types/SlateEnums.h"
#include "FPSRLChatWidget.generated.h"

class UBorder;
class UEditableTextBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/**
 * The session chat on the HUD (bottom left, above the health bar; layout built in code like the other HUD widgets).
 *  - Compact (closed): the last MaxVisibleMessages lines, no background, never takes input. Each line fades after
 *    MessageFadeDelay; fading only hides it, the message stays in the history.
 *  - Open (T / D-pad Left): a dark panel with the scrollable history, a text box and a character counter. Enter sends,
 *    Esc (or gamepad B) cancels and keeps the draft. The game keeps running the whole time.
 * Reads UFPSRLChatSubsystem's client history; owns no chat state itself. Messages are plain text blocks, so no markup.
 */
UCLASS()
class FPSRL_API UFPSRLChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show or hide the input state (the controller handles focus and input mode). */
	void SetOpen(bool bOpen);
	bool IsOpen() const { return bIsOpen; }

	/** The text box to give keyboard focus to when the chat opens. */
	UEditableTextBox* GetInputBox() const { return InputBox; }

	/** A short local line (a refused message), shown for a few seconds; not part of the history. */
	void ShowNotice(const FText& Notice);

	/** Test: type text into the box / press Enter / press Esc, as a player would. */
	void SetDraftForTest(const FString& Text);
	void SubmitForTest() { Submit(); }
	void CancelForTest() { Cancel(); }

	/** Test: what the chat shows right now, as text. */
	FString DescribeForTest() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildLayout();
	void Rebuild();
	void AddLine(const FFPSRLChatMessage& Message, double ArrivedAt);
	void HandleMessageAdded(const FFPSRLChatMessage& Message);
	void HandleHistoryReset();
	void RefreshLines();
	void StartFadeTimer();
	void Submit();
	void Cancel();

	UFUNCTION()
	void HandleTextChanged(const FText& Text);

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> HistoryBox;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> Lines;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> InputRow;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> InputBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Counter;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NoticeText;

	/** One shown line: its widget and when it arrived on this machine (for the compact fade). */
	struct FLine
	{
		TWeakObjectPtr<UWidget> Widget;
		double ArrivedAt = 0.0;
		int32 MessageId = 0;
	};
	TArray<FLine> LineWidgets;

	bool bIsOpen = false;
	bool bUpdatingText = false;
	double NoticeUntil = 0.0;
	FTimerHandle FadeTimer;
	FDelegateHandle AddedHandle;
	FDelegateHandle ResetHandle;
	FDelegateHandle SettingsHandle;
	FString LastShownText;
};
