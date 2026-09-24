// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSubsystemUtils.h"
#include "UI/FPSRLPauseMenuWidget.h"
#include "Core/FPSRLPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "FPSRL.h"

// --- Lobby -------------------------------------------------------------------------------------------------------

void AFPSRLPlayerController::ServerToggleReady_Implementation()
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->SetIsReady(!PS->bIsReady);
	}
}

void AFPSRLPlayerController::ServerRequestStartMatch_Implementation()
{
	// On the server, only the listen host's own controller is local.
	if (!IsLocalController())
	{
		UE_LOG(LogFPSRL, Warning, TEXT("Start Match ignored: only the host can start"));
		return;
	}
	if (!AreAllPlayersReady())
	{
		UE_LOG(LogFPSRL, Warning, TEXT("Start Match ignored: not every player is ready"));
		return;
	}
	if (MatchMap.IsNull())
	{
		UE_LOG(LogFPSRL, Error, TEXT("Start Match: MatchMap is not set on %s"), *GetClass()->GetName());
		return;
	}

	// Seamless travel (GameMode bUseSeamlessTravel) keeps everyone connected and carries PlayerState data.
	GetWorld()->ServerTravel(MatchMap.GetLongPackageName(), false);
}

void AFPSRLPlayerController::ServerSelectWeapon_Implementation(FGameplayTag WeaponTag)
{
	if (!FindWeaponOption(WeaponTag))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("ServerSelectWeapon: %s is not a lobby weapon option"), *WeaponTag.ToString());
		return;
	}

	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->SetSelectedWeapon(WeaponTag);
		EquipSelectedWeapon(GetPawn());
	}
}

bool AFPSRLPlayerController::AreAllPlayersReady() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState || GameState->PlayerArray.IsEmpty())
	{
		return false;
	}

	for (const APlayerState* EachPlayerState : GameState->PlayerArray)
	{
		const AFPSRLPlayerState* LobbyPlayer = Cast<AFPSRLPlayerState>(EachPlayerState);
		if (!LobbyPlayer || !LobbyPlayer->bIsReady)
		{
			return false;
		}
	}
	return true;
}

void AFPSRLPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Server-only (OnPossess never runs on clients). In the lobby players choose at the weapon station instead.
	if (UGameplayStatics::GetCurrentLevelName(this, true) != LobbyLevelName)
	{
		EquipSelectedWeapon(InPawn);
	}
}

const FFPSRLWeaponOption* AFPSRLPlayerController::FindWeaponOption(const FGameplayTag& WeaponTag) const
{
	return WeaponOptions.FindByPredicate([&WeaponTag](const FFPSRLWeaponOption& Option) { return Option.WeaponTag == WeaponTag; });
}

void AFPSRLPlayerController::EquipSelectedWeapon(APawn* InPawn)
{
	if (!HasAuthority() || !InPawn || WeaponOptions.IsEmpty())
	{
		return;
	}

	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const FFPSRLWeaponOption* Option = PS ? FindWeaponOption(PS->SelectedWeapon) : nullptr;
	if (!Option)
	{
		Option = &WeaponOptions[0];
	}
	if (!Option->WeaponClass)
	{
		return;
	}

	// Bridge until the character moves to C++ (Step F): the character receives weapons through the Blueprint
	// interface function "Add Weapon Class" on BPI_WeaponHolder, so call it by name via reflection.
	// Blueprint interface functions keep their display name (with spaces) as the function name.
	UFunction* AddWeaponFunction = InPawn->FindFunction(TEXT("Add Weapon Class"));
	if (!AddWeaponFunction)
	{
		AddWeaponFunction = InPawn->FindFunction(TEXT("AddWeaponClass"));
	}
	if (!AddWeaponFunction)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("%s has no AddWeaponClass; cannot equip %s"), *InPawn->GetName(), *Option->WeaponTag.ToString());
		return;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(AddWeaponFunction->ParmsSize));
	FMemory::Memzero(Params, AddWeaponFunction->ParmsSize);
	bool bParamSet = false;
	for (TFieldIterator<FProperty> It(AddWeaponFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (const FClassProperty* ClassParam = CastField<FClassProperty>(*It))
		{
			if (Option->WeaponClass->IsChildOf(ClassParam->MetaClass))
			{
				ClassParam->SetObjectPropertyValue_InContainer(Params, Option->WeaponClass);
				bParamSet = true;
			}
			break;
		}
	}
	if (!bParamSet)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("AddWeaponClass on %s does not accept %s"), *InPawn->GetName(), *GetNameSafe(Option->WeaponClass));
		return;
	}
	InPawn->ProcessEvent(AddWeaponFunction, Params);

	UE_LOG(LogFPSRL, Log, TEXT("Equipped %s on %s (%s)"), *Option->WeaponTag.ToString(), *InPawn->GetName(),
		PS ? *PS->GetPlayerName() : TEXT("no PlayerState"));
}

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
