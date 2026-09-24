// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSubsystemUtils.h"
#include "UI/FPSRLPauseMenuWidget.h"
#include "UI/FPSRLSelectionWidget.h"
#include "Components/FPSRLAspectComponent.h"
#include "Components/FPSRLBoonComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
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

		// Lobby flow: Choose Weapon -> Choose Aspect. A new weapon gets its own aspect choices.
		if (IsInLobby())
		{
			PS->GetAspectComponent()->OfferAspects(WeaponTag);
		}
	}
}

void AFPSRLPlayerController::ServerSelectAspect_Implementation(int32 EventId, int32 OptionIndex)
{
	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	if (PS && IsInLobby())
	{
		PS->GetAspectComponent()->TrySelectAspect(EventId, OptionIndex, PS->SelectedWeapon);
	}
}

void AFPSRLPlayerController::ServerSelectBoon_Implementation(int32 EventId, int32 OptionIndex)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TrySelect(EventId, OptionIndex);
	}
}

void AFPSRLPlayerController::ServerRerollBoons_Implementation(int32 EventId)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TryReroll(EventId);
	}
}

void AFPSRLPlayerController::ServerReportTalentEssence_Implementation(int32 Amount)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->ReceiveReportedTalentEssence(Amount);
	}
}

void AFPSRLPlayerController::ClientPersistentCurrencyChanged_Implementation(int32 NewAmount)
{
	WriteLocalTalentEssence(NewAmount);
}

void AFPSRLPlayerController::FPSRLGiveBoon(const FString& BoonAssetPath)
{
#if !UE_BUILD_SHIPPING
	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	UFPSRLBoonDefinition* Boon = LoadObject<UFPSRLBoonDefinition>(nullptr, *BoonAssetPath);
	if (!PS || !PS->HasAuthority() || !Boon || !PS->GetBoonComponent()->GrantBoon(Boon))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("FPSRLGiveBoon failed (host only; valid boon path; slots/stacks available): %s"), *BoonAssetPath);
	}
#endif
}

void AFPSRLPlayerController::FPSRLStartBoonSelection()
{
#if !UE_BUILD_SHIPPING
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>(); GameState && HasAuthority())
	{
		GameState->StartBoonSelection();
		OpenBoonSelection();	// no room/terminal involved: open the host's screen directly
	}
	else
	{
		UE_LOG(LogFPSRL, Warning, TEXT("FPSRLStartBoonSelection: host only, in a level using AFPSRLGameState"));
	}
#endif
}

bool AFPSRLPlayerController::IsInLobby() const
{
	return UGameplayStatics::GetCurrentLevelName(this, true) == LobbyLevelName;
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
		if (!LobbyPlayer || !LobbyPlayer->bIsReady || !LobbyPlayer->HasCompletedLoadout())
		{
			return false;
		}
	}
	return true;
}

void AFPSRLPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Server-only (OnPossess never runs on clients).
	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	if (IsInLobby())
	{
		// Back from a run (or first arrival): drop temporary Boon/Aspect state. Aspects are offered only once the
		// player picks a weapon at the station (ServerSelectWeapon); the pawn starts empty-handed here.
		if (PS)
		{
			PS->ClearRunState();
		}
	}
	else
	{
		if (PS)
		{
			PS->BeginRunState();	// applies the aspect's GAS grants (once per run)
		}
		EquipSelectedWeapon(InPawn);
	}
}

void AFPSRLPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindToPlayerStateComponents();
	ReportTalentEssenceIfNeeded();
}

// --- Local selection UI ------------------------------------------------------------------------------------------

void AFPSRLPlayerController::BindToPlayerStateComponents()
{
	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	if (!IsLocalController() || !PS || BoundPlayerState == PS)
	{
		return;
	}
	BoundPlayerState = PS;

	PS->GetAspectComponent()->OnAspectStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshAspectSelectionUI);
	PS->GetBoonComponent()->OnBoonStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshBoonSelectionUI);
	RefreshAspectSelectionUI();
	RefreshBoonSelectionUI();
}

void AFPSRLPlayerController::RefreshAspectSelectionUI()
{
	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const UFPSRLAspectComponent* Aspects = PS ? PS->GetAspectComponent() : nullptr;
	if (!IsLocalController() || !Aspects || Aspects->ActiveAspect || Aspects->AspectOptions.IsEmpty() || !IsInLobby())
	{
		HideSelectionWidget(AspectSelectionWidget);
		return;
	}

	ShowSelectionWidget(AspectSelectionWidget, AspectSelectionClass);
	TArray<FText> Names, Descriptions;
	for (const UFPSRLAspectDefinition* Aspect : Aspects->AspectOptions)
	{
		Names.Add(Aspect ? Aspect->DisplayName : FText::GetEmpty());
		Descriptions.Add(Aspect ? Aspect->Description : FText::GetEmpty());
	}
	AspectSelectionWidget->SetTitle(NSLOCTEXT("FPSRL", "ChooseAspect", "CHOOSE YOUR ASPECT"));
	AspectSelectionWidget->SetChoices(Names, Descriptions);
	AspectSelectionWidget->SetReroll(false, FText::GetEmpty(), false);
	AspectSelectionWidget->SetDeadline(0.0);
	AspectSelectionWidget->SetCloseVisible(false);
	AspectSelectionWidget->OnChoice.BindUObject(this, &ThisClass::HandleAspectChoice);
}

void AFPSRLPlayerController::RefreshBoonSelectionUI()
{
	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
	if (!IsLocalController() || !Boons || !Boons->bSelectionPending || Boons->CurrentOptions.IsEmpty())
	{
		bBoonSelectionOpen = false;	// resolved (picked / timed out): the next selection needs the terminal again
		HideSelectionWidget(BoonSelectionWidget);
		return;
	}
	if (!bBoonSelectionOpen)
	{
		HideSelectionWidget(BoonSelectionWidget);	// pending, but the player hasn't opened it at a terminal
		return;
	}

	ShowSelectionWidget(BoonSelectionWidget, BoonSelectionClass);
	TArray<FText> Names, Descriptions;
	for (const UFPSRLBoonDefinition* Boon : Boons->CurrentOptions)
	{
		Names.Add(Boon ? Boon->DisplayName : FText::GetEmpty());
		Descriptions.Add(Boon ? Boon->Description : FText::GetEmpty());
	}

	const int32 Cost = Boons->GetNextRerollCost();
	const FText RerollLabel = Cost == 0
		? FText::Format(NSLOCTEXT("FPSRL", "RerollFree", "Reroll ({0} free)"), Boons->FreeRerollsRemaining)
		: FText::Format(NSLOCTEXT("FPSRL", "RerollCost", "Reroll ({0} Soul Fragments, have {1})"), Cost, PS->TalentEssence);

	BoonSelectionWidget->SetTitle(NSLOCTEXT("FPSRL", "ChooseBoon", "CHOOSE A BOON"));
	BoonSelectionWidget->SetChoices(Names, Descriptions);
	BoonSelectionWidget->SetReroll(true, RerollLabel, Cost == 0 || PS->TalentEssence >= Cost);
	BoonSelectionWidget->SetDeadline(Boons->SelectionDeadline);
	BoonSelectionWidget->OnChoice.BindUObject(this, &ThisClass::HandleBoonChoice);
	BoonSelectionWidget->OnReroll.BindUObject(this, &ThisClass::HandleBoonReroll);
	BoonSelectionWidget->SetCloseVisible(true);
	BoonSelectionWidget->OnClose.BindUObject(this, &ThisClass::HandleBoonClose);
}

bool AFPSRLPlayerController::HasPendingBoonSelection() const
{
	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
	return Boons && Boons->bSelectionPending && !Boons->CurrentOptions.IsEmpty();
}

bool AFPSRLPlayerController::OpenBoonSelection()
{
	if (!IsLocalController() || !HasPendingBoonSelection())
	{
		return false;
	}
	bBoonSelectionOpen = true;
	RefreshBoonSelectionUI();
	return true;
}

void AFPSRLPlayerController::HandleBoonClose()
{
	// Only hides the screen; the choice stays pending on the server (and still auto-picks on timeout).
	bBoonSelectionOpen = false;
	RefreshBoonSelectionUI();
}

void AFPSRLPlayerController::ShowSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget, TSubclassOf<UFPSRLSelectionWidget> WidgetClass)
{
	if (!Widget)
	{
		Widget = CreateWidget<UFPSRLSelectionWidget>(this, WidgetClass ? WidgetClass : TSubclassOf<UFPSRLSelectionWidget>(UFPSRLSelectionWidget::StaticClass()));
	}
	if (Widget && !Widget->IsInViewport())
	{
		Widget->AddToViewport(50);
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(Widget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		SetShowMouseCursor(true);
	}
}

void AFPSRLPlayerController::HideSelectionWidget(TObjectPtr<UFPSRLSelectionWidget>& Widget)
{
	if (Widget && Widget->IsInViewport())
	{
		Widget->RemoveFromParent();
		const bool bOtherSelectionOpen = (AspectSelectionWidget && AspectSelectionWidget->IsInViewport())
			|| (BoonSelectionWidget && BoonSelectionWidget->IsInViewport());
		if (!bOtherSelectionOpen && !IsPauseMenuOpen())
		{
			SetInputMode(FInputModeGameOnly());
			SetShowMouseCursor(false);
		}
	}
}

void AFPSRLPlayerController::HandleAspectChoice(int32 OptionIndex)
{
	if (const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		ServerSelectAspect(PS->GetAspectComponent()->AspectEventId, OptionIndex);
	}
}

void AFPSRLPlayerController::HandleBoonChoice(int32 OptionIndex)
{
	if (const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		ServerSelectBoon(PS->GetBoonComponent()->SelectionEventId, OptionIndex);
	}
}

void AFPSRLPlayerController::HandleBoonReroll()
{
	if (const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		ServerRerollBoons(PS->GetBoonComponent()->SelectionEventId);
	}
}

// --- Local profile currency bridge ------------------------------------------------------------------------------
// The persistent balance lives in BP_SaveGame_PlayerProfile (Blueprint) held by BP_GameInstanceBase. Until the save
// game moves to C++, read/write it by property name, the same bridge pattern as the weapon equip call.

namespace
{
	UObject* GetLocalProfile(const UGameInstance* GameInstance)
	{
		const FObjectProperty* ProfileProperty = GameInstance ? FindFProperty<FObjectProperty>(GameInstance->GetClass(), TEXT("CurrentPlayerProfile")) : nullptr;
		return ProfileProperty ? ProfileProperty->GetObjectPropertyValue_InContainer(GameInstance) : nullptr;
	}

	FNumericProperty* GetTalentEssenceProperty(const UObject* Profile)
	{
		return Profile ? FindFProperty<FNumericProperty>(Profile->GetClass(), TEXT("TalentEssence")) : nullptr;
	}
}

bool AFPSRLPlayerController::ReadLocalTalentEssence(int32& OutAmount) const
{
	const UObject* Profile = GetLocalProfile(GetGameInstance());
	const FNumericProperty* Property = GetTalentEssenceProperty(Profile);
	if (!Property)
	{
		return false;
	}
	const void* Value = Property->ContainerPtrToValuePtr<void>(Profile);
	OutAmount = Property->IsFloatingPoint() ? FMath::FloorToInt(Property->GetFloatingPointPropertyValue(Value)) : static_cast<int32>(Property->GetSignedIntPropertyValue(Value));
	return true;
}

void AFPSRLPlayerController::WriteLocalTalentEssence(int32 Amount) const
{
	UGameInstance* GameInstance = GetGameInstance();
	UObject* Profile = GetLocalProfile(GameInstance);
	FNumericProperty* Property = GetTalentEssenceProperty(Profile);
	if (!Property)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("Could not persist Soul Fragments: no CurrentPlayerProfile.TalentEssence on the Game Instance"));
		return;
	}
	void* Value = Property->ContainerPtrToValuePtr<void>(Profile);
	if (Property->IsFloatingPoint())
	{
		Property->SetFloatingPointPropertyValue(Value, static_cast<double>(Amount));
	}
	else
	{
		Property->SetIntPropertyValue(Value, static_cast<int64>(Amount));
	}

	if (UFunction* SaveFunction = GameInstance->FindFunction(TEXT("SaveCurrentProfile")); SaveFunction && SaveFunction->ParmsSize == 0)
	{
		GameInstance->ProcessEvent(SaveFunction, nullptr);
	}
}

void AFPSRLPlayerController::ReportTalentEssenceIfNeeded()
{
	APlayerState* PS = PlayerState;
	if (!IsLocalController() || !PS || ReportedPlayerState == PS)
	{
		return;
	}
	int32 Amount = 0;
	if (ReadLocalTalentEssence(Amount))
	{
		ReportedPlayerState = PS;
		ServerReportTalentEssence(Amount);
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

	// Listen host / cases where the PlayerState already exists (clients also get OnRep_PlayerState).
	BindToPlayerStateComponents();
	ReportTalentEssenceIfNeeded();
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
	for (UFPSRLSelectionWidget* Widget : { AspectSelectionWidget.Get(), BoonSelectionWidget.Get() })
	{
		if (Widget)
		{
			Widget->RemoveFromParent();
		}
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
