// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSubsystemUtils.h"
#include "UI/FPSRLPauseMenuWidget.h"
#include "FPSRL.h"

void AFPSRLPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController() && PauseMappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// High priority so no gameplay mapping can shadow the pause key.
			Subsystem->AddMappingContext(PauseMappingContext, 100);
		}
	}
}

void AFPSRLPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (PauseAction)
		{
			EnhancedInput->BindAction(PauseAction, ETriggerEvent::Started, this, &ThisClass::TogglePauseMenu);
		}
	}
}

void AFPSRLPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PauseMenu)
	{
		PauseMenu->RemoveFromParent();
		PauseMenu = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSRLPlayerController::TogglePauseMenu()
{
	IsPauseMenuOpen() ? ClosePauseMenu() : OpenPauseMenu();
}

bool AFPSRLPlayerController::IsPauseMenuOpen() const
{
	return PauseMenu && PauseMenu->IsInViewport();
}

void AFPSRLPlayerController::OpenPauseMenu()
{
	if (!IsLocalController() || IsPauseMenuOpen())
	{
		return;
	}

	if (!PauseMenu)
	{
		const TSubclassOf<UFPSRLPauseMenuWidget> WidgetClass = PauseMenuClass ? PauseMenuClass : TSubclassOf<UFPSRLPauseMenuWidget>(UFPSRLPauseMenuWidget::StaticClass());
		PauseMenu = CreateWidget<UFPSRLPauseMenuWidget>(this, WidgetClass);
	}
	if (!PauseMenu)
	{
		return;
	}

	bCursorWasVisible = ShouldShowMouseCursor();
	PauseMenu->AddToViewport(100);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	FlushPressedKeys();
}

void AFPSRLPlayerController::ClosePauseMenu()
{
	if (!IsPauseMenuOpen())
	{
		return;
	}

	PauseMenu->RemoveFromParent();

	if (bCursorWasVisible)
	{
		// e.g. the Lobby's corner UI was up: keep the cursor, but let gameplay input through again.
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
	FlushPressedKeys();
}

void AFPSRLPlayerController::QuitToMainMenu()
{
	ClosePauseMenu();

	// End the Steam session first so the next Host doesn't fail with "session already exists".
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		DestroySessionHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete));
		if (Sessions->DestroySession(NAME_GameSession))
		{
			return; // travel continues in HandleDestroySessionComplete
		}
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->ReturnToMainMenu();
	}
}

void AFPSRLPlayerController::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}

	UE_LOG(LogFPSRL, Log, TEXT("Session '%s' destroyed (success=%d); returning to main menu"), *SessionName.ToString(), bWasSuccessful);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->ReturnToMainMenu();
	}
}
