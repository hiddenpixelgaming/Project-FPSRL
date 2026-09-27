// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLServerBrowserWidget.generated.h"

class UButton;
class UScrollBox;
class UTextBlock;
class UFPSRLServerBrowserWidget;

/** One row's Join button (UButton clicks carry no data, so each row gets its own handler). */
UCLASS()
class UFPSRLServerBrowserRowHandler : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UFPSRLServerBrowserWidget> Browser;
	int32 ResultIndex = INDEX_NONE;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Menu's "Join" screen: every hosted FPSRL game with its host, player count and status (Waiting in Lobby / Run in
 * progress), plus Refresh and Back. Joining a game whose run has started shows "Unable to join because a run is in
 * session." (the host refuses it too, see UFPSRLSessionSubsystem). Opened by AFPSRLPlayerControllerMenu.
 *
 * Works with no Blueprint: the layout is built in code. Esc closes it.
 */
UCLASS()
class FPSRL_API UFPSRLServerBrowserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show a line under the title (e.g. why the last join failed). */
	void ShowMessage(const FText& Message, bool bIsError);

	void JoinRow(int32 ResultIndex);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildLayout();
	UButton* MakeButton(const FName& Name, const FText& Label, float FontSize);
	UTextBlock* MakeText(const FText& Text, float FontSize, const FLinearColor& Color);

	void HandleSessionsFound(bool bSuccess);
	void HandleJoinFailed(const FText& Reason);
	void RebuildRows();

	void HandleRefresh();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleBack();

	class UFPSRLSessionSubsystem* GetSessions() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RowList;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> BackButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFPSRLServerBrowserRowHandler>> RowHandlers;

	FDelegateHandle SessionsFoundHandle;
	FDelegateHandle JoinFailedHandle;

	/** An error is on screen: searches don't replace it until the player acts (Refresh or Join). */
	bool bErrorShown = false;
};
