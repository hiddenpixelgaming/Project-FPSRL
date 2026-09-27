// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerControllerMenu.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Core/FPSRLSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "UI/FPSRLServerBrowserWidget.h"
#include "FPSRL.h"

AFPSRLPlayerControllerMenu::AFPSRLPlayerControllerMenu()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AFPSRLPlayerControllerMenu::BeginPlay()
{
	Super::BeginPlay();	// BP_PlayerControllerMenu creates WB_Menu here

	if (IsLocalController())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		SetShowMouseCursor(true);

		TakeOverJoinButton();

		// Back from a join the host refused (or that couldn't connect): straight back to the list, with the reason.
		const UGameInstance* GameInstance = GetGameInstance();
		UFPSRLSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UFPSRLSessionSubsystem>() : nullptr;
		const FText JoinError = Sessions ? Sessions->ConsumePendingJoinError() : FText::GetEmpty();
		if (!JoinError.IsEmpty())
		{
			OpenServerBrowser();
			if (ServerBrowser)
			{
				ServerBrowser->ShowMessage(JoinError, true);
			}
		}
	}
}

void AFPSRLPlayerControllerMenu::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}

	Super::EndPlay(EndPlayReason);
}

void AFPSRLPlayerControllerMenu::TakeOverJoinButton()
{
	// Done from C++ so the Menu Blueprint needs no edit; its old handler is unbound, not deleted.
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Widget : Widgets)
	{
		UButton* JoinButton = Widget && Widget->WidgetTree ? Widget->WidgetTree->FindWidget<UButton>(TEXT("JoinButton")) : nullptr;
		if (JoinButton)
		{
			JoinButton->OnClicked.Clear();
			JoinButton->OnClicked.AddDynamic(this, &ThisClass::OpenServerBrowser);
			UE_LOG(LogFPSRL, Log, TEXT("[Menu] %s JoinButton opens the server browser"), *Widget->GetClass()->GetName());
			return;
		}
	}
	UE_LOG(LogFPSRL, Warning, TEXT("[Menu] No JoinButton found on the Menu widget; the server browser is unreachable"));
}

void AFPSRLPlayerControllerMenu::OpenServerBrowser()
{
	if (ServerBrowser && ServerBrowser->IsInViewport())
	{
		return;
	}
	ServerBrowser = CreateWidget<UFPSRLServerBrowserWidget>(this, ServerBrowserClass ? ServerBrowserClass : TSubclassOf<UFPSRLServerBrowserWidget>(UFPSRLServerBrowserWidget::StaticClass()));
	if (ServerBrowser)
	{
		ServerBrowser->AddToViewport(10);
		ServerBrowser->SetKeyboardFocus();
	}
}
