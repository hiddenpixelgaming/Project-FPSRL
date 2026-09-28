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
#include "Components/FPSRLRelicComponent.h"
#include "Components/FPSRLBoonComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerState.h"
#include "Core/FPSRLRunSubsystem.h"
#include "EngineUtils.h"
#include "Rooms/FPSRLBoonTerminal.h"
#include "Rooms/FPSRLExitPortal.h"
#include "Rooms/FPSRLReviveMarker.h"
#include "UI/FPSRLPortalWidgets.h"
#include "UI/FPSRLReviveWidget.h"
#include "UI/FPSRLEncounterBarWidget.h"
#include "Rooms/FPSRLRoom.h"
#include "Data/FPSRLEncounterDefinition.h"
#include "Core/FPSRLAutopilotSubsystem.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "UI/FPSRLDeathMenuWidget.h"
#include "Components/FPSRLHealthComponent.h"
#include "Combat/FPSRLProjectile.h"
#include "InputMappingContext.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Data/FPSRLAspectDefinition.h"
#include "Data/FPSRLRelicDefinition.h"
#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLBoonSettings.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
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
	// The run (Project Settings > FPSRL Run) travels to its first Depth. Without one, MatchMap is a one-Depth run.
	if (UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
		RunSubsystem && RunSubsystem->StartRun(GetWorld()))
	{
		return;
	}
	if (MatchMap.IsNull())
	{
		UE_LOG(LogFPSRL, Error, TEXT("Start Match: no Run Definition in Project Settings > FPSRL Run and no MatchMap on %s"), *GetClass()->GetName());
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

void AFPSRLPlayerController::ServerChooseAspect_Implementation(int32 EventId, int32 OptionIndex)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TryChooseAspect(EventId, OptionIndex);
	}
}

void AFPSRLPlayerController::ServerChooseSlot_Implementation(int32 EventId, int32 OptionIndex)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TryChooseSlot(EventId, OptionIndex);
	}
}

void AFPSRLPlayerController::ServerAltarBack_Implementation(int32 EventId)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TryBack(EventId);
	}
}

void AFPSRLPlayerController::ServerSelectUpgrade_Implementation(int32 EventId, int32 OptionIndex)
{
	if (AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		PS->GetBoonComponent()->TrySelectUpgrade(EventId, OptionIndex);
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

// --- Test commands (development builds) --------------------------------------------------------------------------

void AFPSRLPlayerController::FPSRLGiveBoon(const FString& BoonAsset, const FString& Channel)
{
	ServerTestCommand(TEXT("GiveBoon"), BoonAsset, Channel);
}

void AFPSRLPlayerController::FPSRLOfferBoon()
{
	bBoonSelectionOpen = true;	// show the choice as soon as it replicates
	ServerTestCommand(TEXT("OfferBoon"), FString(), FString());
}

void AFPSRLPlayerController::FPSRLOfferUpgrade()
{
	bBoonSelectionOpen = true;
	ServerTestCommand(TEXT("OfferUpgrade"), FString(), FString());
}

void AFPSRLPlayerController::FPSRLGiveRelic(const FString& RelicAsset)
{
	ServerTestCommand(TEXT("GiveRelic"), RelicAsset, FString());
}

void AFPSRLPlayerController::FPSRLPick(int32 OptionIndex)
{
	HandleBoonChoice(OptionIndex);	// same request the screen sends
}

void AFPSRLPlayerController::FPSRLReroll()
{
	HandleBoonReroll();	// same request the Reroll button sends
}

void AFPSRLPlayerController::FPSRLBack()
{
	HandleBoonBack();	// same request the Back button sends
}

void AFPSRLPlayerController::FPSRLAutoRun(int32 TestFall)
{
	if (HasAuthority() && GetGameInstance())
	{
		GetGameInstance()->GetSubsystem<UFPSRLAutopilotSubsystem>()->Start(TestFall != 0);
	}
}

namespace
{
	/** A pool entry by asset name (DA_Boon_X), or any asset by full path. */
	template <typename T>
	T* FindTestAsset(const FString& NameOrPath, const TArray<TObjectPtr<T>>* Pool)
	{
		if (NameOrPath.Contains(TEXT("/")))
		{
			return LoadObject<T>(nullptr, *NameOrPath);
		}
		if (Pool)
		{
			for (T* Asset : *Pool)
			{
				if (Asset && Asset->GetName().Equals(NameOrPath, ESearchCase::IgnoreCase))
				{
					return Asset;
				}
			}
		}
		return nullptr;
	}
}

void AFPSRLPlayerController::ServerTestCommand_Implementation(FName Command, const FString& Arg1, const FString& Arg2)
{
#if !UE_BUILD_SHIPPING
	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	if (!PS)
	{
		return;
	}
	UFPSRLBoonComponent* Boons = PS->GetBoonComponent();
	bool bDone = false;
	if (Command == TEXT("GiveBoon"))
	{
		const UFPSRLBoonPool* Pool = UFPSRLBoonSettings::Get().BoonPool.LoadSynchronous();
		UFPSRLBoonDefinition* Boon = FindTestAsset<UFPSRLBoonDefinition>(Arg1, Pool ? &Pool->Boons : nullptr);
		const int64 ChannelValue = StaticEnum<EFPSRLBoonChannel>()->GetValueByNameString(Arg2.IsEmpty() ? TEXT("Primary") : Arg2);
		bDone = Boon && ChannelValue != INDEX_NONE && Boons->GrantBoon(Boon, static_cast<EFPSRLBoonChannel>(ChannelValue));
	}
	else if (Command == TEXT("OfferBoon"))
	{
		bDone = Boons->BeginAltar();	// like a Blessing altar: upgrades once every Blessing is taken
	}
	else if (Command == TEXT("OfferUpgrade"))
	{
		bDone = Boons->BeginUpgradeSelection();
	}
	else if (Command == TEXT("GiveRelic"))
	{
		UFPSRLRelicComponent* Relics = PS->GetRelicComponent();
		if (Arg1.IsEmpty())
		{
			bDone = Relics->GrantRandomRelic() != nullptr;
		}
		else
		{
			const UFPSRLRelicPool* Pool = UFPSRLBoonSettings::Get().RelicPool.LoadSynchronous();
			bDone = Relics->GrantRelic(FindTestAsset<UFPSRLRelicDefinition>(Arg1, Pool ? &Pool->Relics : nullptr));
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Test] %s %s %s for %s: %s"), *Command.ToString(), *Arg1, *Arg2, *PS->GetPlayerName(),
		bDone ? TEXT("done") : TEXT("refused (unknown asset, not eligible, a choice already open, or nothing to offer)"));
	if (!bDone)
	{
		// An altar command with nothing to open (and no choice already open) says so, like a real altar.
		const bool bNothingLeft = (Command == TEXT("OfferBoon") || Command == TEXT("OfferUpgrade")) && !Boons->HasPendingSelection();
		ClientBoonAltarRejected(bNothingLeft ? NSLOCTEXT("FPSRL", "NothingLeft", "Nothing left to offer") : FText::GetEmpty());
	}
#endif
}

// --- Death ---------------------------------------------------------------------------------------------------------

bool AFPSRLPlayerController::IsAnyPlayerAlive(const UWorld* World)
{
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return false;
	}
	// Downed players are not "alive" here: they can't end or continue the run on their own.
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		const APawn* Pawn = Player && !Player->IsInactive() ? Player->GetPawn() : nullptr;
		if (UFPSRLHealthComponent::IsPawnUp(Pawn))
		{
			return true;
		}
	}
	return false;
}

void AFPSRLPlayerController::ServerStartRevive_Implementation(AFPSRLReviveMarker* Marker)
{
	if (Marker)
	{
		Marker->TryStartRevive(this);
	}
}

void AFPSRLPlayerController::ShowReviveProgress(const FText& Label, double StartTime, double EndTime)
{
	if (!IsLocalController())
	{
		return;
	}
	if (!ReviveWidget)
	{
		ReviveWidget = CreateWidget<UFPSRLReviveWidget>(this, ReviveWidgetClass ? ReviveWidgetClass : TSubclassOf<UFPSRLReviveWidget>(UFPSRLReviveWidget::StaticClass()));
	}
	if (ReviveWidget)
	{
		ReviveWidget->ShowRevive(Label, StartTime, EndTime);
	}
}

void AFPSRLPlayerController::HideReviveProgress(const FText& Message)
{
	if (ReviveWidget)
	{
		ReviveWidget->HideRevive(Message);
	}
}

void AFPSRLPlayerController::HandlePawnDied(AController* Killer, AActor* Causer)
{
	const bool bPartyDown = !IsAnyPlayerAlive(GetWorld());
	UE_LOG(LogFPSRL, Log, TEXT("%s died%s"), *GetNameSafe(PlayerState), bPartyDown ? TEXT(": the whole party is down") : TEXT(""));
	ClientShowDeathScreen(bPartyDown);

	if (bPartyDown)
	{
		// Players who died earlier were told to wait for their team; now they may leave too.
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AFPSRLPlayerController* Other = Cast<AFPSRLPlayerController>(It->Get());
			if (Other && Other != this)
			{
				Other->ClientPartyDown();
			}
		}
	}
}

void AFPSRLPlayerController::ClientShowDeathScreen_Implementation(bool bCanReturnToLobby)
{
	bDeathCanReturn = bCanReturnToLobby;

	// Nothing else stays up over the death screen.
	ClosePauseMenu();
	ClosePortalMenu();
	bBoonSelectionOpen = false;
	HideSelectionWidget(BoonSelectionWidget);

	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(0.f, 1.f, DeathFadeSeconds, FLinearColor::Black, false, /*bHoldWhenFinished*/ true);
	}
	if (DeathFadeSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(DeathFadeTimer, this, &ThisClass::ShowDeathMenu, DeathFadeSeconds, false);
	}
	else
	{
		ShowDeathMenu();
	}
}

void AFPSRLPlayerController::ShowDeathMenu()
{
	if (!DeathMenu)
	{
		DeathMenu = CreateWidget<UFPSRLDeathMenuWidget>(this, DeathMenuClass ? DeathMenuClass : TSubclassOf<UFPSRLDeathMenuWidget>(UFPSRLDeathMenuWidget::StaticClass()));
		DeathMenu->OnReturnToLobby.BindWeakLambda(this, [this]() { ServerReturnToLobbyAfterDeath(); });
		DeathMenu->OnQuitGame.BindWeakLambda(this, [this]() { UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); });
	}
	DeathMenu->SetCanReturn(bDeathCanReturn);
	if (!DeathMenu->IsInViewport())
	{
		DeathMenu->AddToViewport(60);
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(DeathMenu->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		SetShowMouseCursor(true);
	}
}

void AFPSRLPlayerController::ClientPartyDown_Implementation()
{
	bDeathCanReturn = true;
	if (DeathMenu)
	{
		DeathMenu->SetCanReturn(true);
	}
}

void AFPSRLPlayerController::ServerReturnToLobbyAfterDeath_Implementation()
{
	if (IsAnyPlayerAlive(GetWorld()))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("Return to Lobby ignored: a teammate is still alive"));
		return;
	}
	if (UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		RunSubsystem->EndRun(GetWorld(), false);
	}
}

// --- Exit portal ---------------------------------------------------------------------------------------------------

bool AFPSRLPlayerController::IsAnyModalOpen() const
{
	return (BoonSelectionWidget && BoonSelectionWidget->IsInViewport())
		|| (PortalMenu && PortalMenu->IsInViewport())
		|| (DeathMenu && DeathMenu->IsInViewport());
}

void AFPSRLPlayerController::OpenPortalMenu(AFPSRLExitPortal* Portal)
{
	if (!IsLocalController() || !Portal)
	{
		return;
	}
	if (!PortalMenu)
	{
		PortalMenu = CreateWidget<UFPSRLPortalMenuWidget>(this, PortalMenuClass ? PortalMenuClass : TSubclassOf<UFPSRLPortalMenuWidget>(UFPSRLPortalMenuWidget::StaticClass()));
		PortalMenu->OnContinue.BindUObject(this, &ThisClass::HandlePortalChoice, true);
		PortalMenu->OnCancel.BindUObject(this, &ThisClass::HandlePortalChoice, false);
	}
	MenuPortal = Portal;
	PortalMenu->ShowPortal(Portal, Portal->HasVotedToContinue(PlayerState));
	if (!PortalMenu->IsInViewport())
	{
		PortalMenu->AddToViewport(50);
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(PortalMenu->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		SetShowMouseCursor(true);
	}
}

void AFPSRLPlayerController::ClosePortalMenu()
{
	MenuPortal.Reset();
	if (PortalMenu && PortalMenu->IsInViewport())
	{
		PortalMenu->RemoveFromParent();
		if (!IsAnyModalOpen() && !IsPauseMenuOpen())
		{
			SetInputMode(FInputModeGameOnly());
			SetShowMouseCursor(false);
		}
	}
}

void AFPSRLPlayerController::HandlePortalChoice(bool bContinue)
{
	// Continue: vote and get back to the game (the HUD tracker shows the group). Cancel: withdraw and close.
	if (AFPSRLExitPortal* Portal = MenuPortal.Get())
	{
		ServerPortalVote(Portal, bContinue);
	}
	ClosePortalMenu();
}

void AFPSRLPlayerController::ServerPortalVote_Implementation(AFPSRLExitPortal* Portal, bool bContinue)
{
	if (Portal)
	{
		Portal->SetContinueVote(this, bContinue);
	}
}

void AFPSRLPlayerController::RefreshPortalUI(AFPSRLExitPortal* Portal)
{
	if (!IsLocalController() || !Portal)
	{
		return;
	}

	const bool bActive = Portal->PortalState == EPortalState::Active;
	if (MenuPortal.Get() == Portal)
	{
		if (bActive)
		{
			PortalMenu->ShowPortal(Portal, Portal->HasVotedToContinue(PlayerState));
		}
		else
		{
			ClosePortalMenu();	// travelling, or no longer usable
		}
	}

	// HUD tracker for everyone while at least one player has chosen Continue.
	if (bActive && !Portal->ContinueVotes.IsEmpty())
	{
		if (!PortalStatus)
		{
			PortalStatus = CreateWidget<UFPSRLPortalStatusWidget>(this, PortalStatusClass ? PortalStatusClass : TSubclassOf<UFPSRLPortalStatusWidget>(UFPSRLPortalStatusWidget::StaticClass()));
		}
		if (!PortalStatus->IsInViewport())
		{
			PortalStatus->AddToViewport(10);
		}
		PortalStatus->ShowPortal(Portal);
	}
	else if (PortalStatus && PortalStatus->IsInViewport())
	{
		PortalStatus->ShowPortal(nullptr);
		PortalStatus->RemoveFromParent();
	}
}

// --- Boon altars ---------------------------------------------------------------------------------------------------

void AFPSRLPlayerController::UseBoonAltar(AFPSRLBoonTerminal* Altar)
{
	if (!IsLocalController() || !Altar)
	{
		return;
	}
	if (OpenBoonSelection())
	{
		return;	// a choice is already open: just show it again
	}
	// Show the screen as soon as the server's options replicate (RefreshBoonSelectionUI).
	bBoonSelectionOpen = true;
	ServerUseBoonAltar(Altar);
}

void AFPSRLPlayerController::ServerUseBoonAltar_Implementation(AFPSRLBoonTerminal* Altar)
{
	FText Reason;
	if (!Altar || !Altar->TryOffer(this, Reason))
	{
		ClientBoonAltarRejected(Reason);
	}
}

void AFPSRLPlayerController::ClientBoonAltarRejected_Implementation(const FText& Reason)
{
	if (!HasPendingBoonSelection())
	{
		bBoonSelectionOpen = false;
	}
	if (!Reason.IsEmpty())
	{
		ShowNotice(Reason);
	}
}

void AFPSRLPlayerController::ShowNotice(const FText& Message)
{
	if (!IsLocalController() || Message.IsEmpty())
	{
		return;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Notice] %s"), *Message.ToString());
	if (!ReviveWidget)
	{
		ReviveWidget = CreateWidget<UFPSRLReviveWidget>(this, ReviveWidgetClass ? ReviveWidgetClass : TSubclassOf<UFPSRLReviveWidget>(UFPSRLReviveWidget::StaticClass()));
	}
	if (ReviveWidget)
	{
		ReviveWidget->HideRevive(Message);	// flashes the message briefly, then hides
	}
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
	if (UFPSRLHealthComponent* Health = InPawn ? InPawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr)
	{
		Health->OnDeath.AddUniqueDynamic(this, &ThisClass::HandlePawnDied);	// OnDeath is server-only too
	}

	AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	if (IsInLobby())
	{
		// Back from a run (or first arrival): drop temporary Blessing / relic state. The pawn starts empty-handed
		// here; the weapon station equips the pick.
		if (PS)
		{
			PS->ClearRunState();
			PS->SetEquippedWeapon(FGameplayTag());
		}
	}
	else
	{
		if (PS)
		{
			PS->BeginRunState();	// re-grants the carried Blessings and relics
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

	PS->GetBoonComponent()->OnBoonStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshBoonSelectionUI);
	RefreshBoonSelectionUI();
}

void AFPSRLPlayerController::RefreshBoonSelectionUI()
{
	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
	if (IsLocalController())
	{
		// An altar's prompt depends on whether this player still has its choice open, and a Blessing altar turns into an
		// Upgrade Altar (look and prompt) once this player has taken every Blessing.
		for (TActorIterator<AFPSRLBoonTerminal> It(GetWorld()); It; ++It)
		{
			It->RefreshLocalAppearance();
			It->RefreshLocalInteractor();
		}
	}
	if (!IsLocalController() || !Boons || !HasPendingBoonSelection())
	{
		bBoonSelectionOpen = false;	// resolved: the next selection needs an altar again
		HideSelectionWidget(BoonSelectionWidget);
		return;
	}
	if (!bBoonSelectionOpen)
	{
		HideSelectionWidget(BoonSelectionWidget);	// pending, but the player hasn't opened it at an altar
		return;
	}

	ShowSelectionWidget(BoonSelectionWidget, BoonSelectionClass);
	TArray<UFPSRLSelectionWidget::FChoice> Choices;
	const FText Dot = NSLOCTEXT("FPSRL", "Separator", " · ");
	auto AspectName = [](const UFPSRLAspectDefinition* Aspect)
	{
		return !Aspect ? FText::GetEmpty() : Aspect->DisplayName.IsEmpty() ? FText::FromString(Aspect->GetName()) : Aspect->DisplayName;
	};
	auto BoonName = [](const UFPSRLBoonDefinition* Boon)
	{
		return !Boon ? FText::GetEmpty() : Boon->DisplayName.IsEmpty() ? FText::FromString(Boon->GetName()) : Boon->DisplayName;
	};

	if (Boons->PendingKind == EFPSRLBoonSelectionKind::Upgrade)
	{
		for (const FFPSRLUpgradeOffer& Offer : Boons->UpgradeOptions)
		{
			const UFPSRLAspectDefinition* Aspect = Boons->GetTrack(Offer.Channel).Aspect;
			const FText Channel = UFPSRLBoonComponent::GetChannelName(Offer.Channel);
			const FFPSRLOwnedBoon* Owned = Boons->GetTrack(Offer.Channel).Boons.FindByPredicate(
				[&Offer](const FFPSRLOwnedBoon& Entry) { return Entry.Boon == Offer.Boon; });
			UFPSRLSelectionWidget::FChoice& Choice = Choices.AddDefaulted_GetRef();
			Choice.bGold = true;	// upgrades are shown in gold, like upgraded Blessings in the build summary
			Choice.HeaderColor = Aspect ? Aspect->Color : FLinearColor::White;
			// Upgrades stack, so show the level this pick would reach.
			Choice.Header = FText::Format(NSLOCTEXT("FPSRL", "BlessingUpgradeHeader", "{0}{1}{2}{1}UPGRADE {3}/{4}"), Channel, Dot, AspectName(Aspect),
				(Owned ? Owned->UpgradeLevel : 0) + 1, Offer.Boon ? Offer.Boon->MaxUpgradeLevel : 0);
			Choice.Name = BoonName(Offer.Boon);
			Choice.Description = Offer.Boon && !Offer.Boon->UpgradeDescription.IsEmpty() ? Offer.Boon->UpgradeDescription
				: NSLOCTEXT("FPSRL", "BlessingUpgradeDefault", "Upgraded: a stronger version of this Blessing.");
		}
		BoonSelectionWidget->SetTitle(NSLOCTEXT("FPSRL", "ChooseUpgrade", "UPGRADE A BLESSING"));
		BoonSelectionWidget->SetReroll(false, FText::GetEmpty(), false);
		BoonSelectionWidget->SetBackVisible(false);
	}
	else if (Boons->AltarStep == EFPSRLAltarStep::ChooseAspect)
	{
		// Step 1: the Aspect. Owned ones say which slot they are on; new ones say a free slot will be chosen.
		for (const UFPSRLAspectDefinition* Aspect : Boons->AspectOptions)
		{
			EFPSRLBoonChannel OwnedOn = EFPSRLBoonChannel::MAX;
			for (const FFPSRLBoonTrack& Track : Boons->Tracks)
			{
				OwnedOn = Track.Aspect == Aspect ? Track.Channel : OwnedOn;
			}
			UFPSRLSelectionWidget::FChoice& Choice = Choices.AddDefaulted_GetRef();
			Choice.Header = OwnedOn != EFPSRLBoonChannel::MAX
				? FText::Format(NSLOCTEXT("FPSRL", "AspectOwnedHeader", "YOUR ASPECT{0}ON {1}{0}{2} BLESSINGS"), Dot,
					UFPSRLBoonComponent::GetChannelName(OwnedOn), Boons->GetTrack(OwnedOn).Count)
				: NSLOCTEXT("FPSRL", "AspectNewHeader", "NEW ASPECT (goes on a free slot)");
			Choice.HeaderColor = Aspect ? Aspect->Color : FLinearColor::White;
			Choice.Name = AspectName(Aspect);
			Choice.Description = Aspect ? Aspect->Description : FText::GetEmpty();
		}
		const int32 Cost = Boons->GetNextRerollCost();
		const FText RerollLabel = Cost == 0
			? FText::Format(NSLOCTEXT("FPSRL", "RerollFree", "Reroll Aspects ({0} free left this run)"), Boons->FreeRerollsRemaining)
			: FText::Format(NSLOCTEXT("FPSRL", "RerollCost", "Reroll Aspects ({0} Soul Fragments, have {1})"), Cost, PS->TalentEssence);
		BoonSelectionWidget->SetTitle(NSLOCTEXT("FPSRL", "ChooseAspect", "CHOOSE AN ASPECT"));
		BoonSelectionWidget->SetReroll(true, RerollLabel, Cost == 0 || PS->TalentEssence >= Cost);
		BoonSelectionWidget->OnReroll.BindUObject(this, &ThisClass::HandleBoonReroll);
		BoonSelectionWidget->SetBackVisible(false);
	}
	else if (Boons->AltarStep == EFPSRLAltarStep::ChooseSlot)
	{
		// Step 2 (new Aspect only): which free slot it goes on.
		for (const EFPSRLBoonChannel Channel : Boons->SlotOptions)
		{
			FString ItemName;
			Boons->GetChannelItem(Channel).GetTagName().ToString().Split(TEXT("."), nullptr, &ItemName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			UFPSRLSelectionWidget::FChoice& Choice = Choices.AddDefaulted_GetRef();
			Choice.Header = FText::Format(NSLOCTEXT("FPSRL", "SlotHeader", "EMPTY SLOT{0}{1}"), Dot, FText::FromString(ItemName.ToUpper()));
			Choice.HeaderColor = Boons->ChosenAspect ? Boons->ChosenAspect->Color : FLinearColor::White;
			Choice.Name = UFPSRLBoonComponent::GetChannelName(Channel);
			Choice.Description = FText::Format(NSLOCTEXT("FPSRL", "SlotDescription", "Put {0} on your {1}. It stays there for the rest of the run."),
				AspectName(Boons->ChosenAspect), UFPSRLBoonComponent::GetChannelName(Channel));
		}
		BoonSelectionWidget->SetTitle(FText::Format(NSLOCTEXT("FPSRL", "ChooseSlot", "CHOOSE A SLOT FOR {0}"), AspectName(Boons->ChosenAspect)));
		BoonSelectionWidget->SetReroll(false, FText::GetEmpty(), false);
		BoonSelectionWidget->SetBackVisible(true);
	}
	else
	{
		// Step 3: the Blessing, which may be the Aspect's Minor or Major.
		for (const FFPSRLBoonOffer& Offer : Boons->CurrentOptions)
		{
			const UFPSRLAspectDefinition* Aspect = Offer.Boon ? Offer.Boon->Aspect.Get() : nullptr;
			// Every Blessing is one of the two categories.
			const FText Kind = Offer.Boon && Offer.Boon->BoonType == EFPSRLBoonType::Major
				? NSLOCTEXT("FPSRL", "MajorBlessing", "MAJOR BLESSING") : NSLOCTEXT("FPSRL", "MinorBlessing", "MINOR BLESSING");
			UFPSRLSelectionWidget::FChoice& Choice = Choices.AddDefaulted_GetRef();
			Choice.Header = FText::Format(NSLOCTEXT("FPSRL", "BlessingHeader", "{0}{1}{2}{1}{3}"),
				UFPSRLBoonComponent::GetChannelName(Offer.Channel), Dot, AspectName(Aspect), Kind);
			Choice.HeaderColor = Aspect ? Aspect->Color : FLinearColor::White;
			Choice.Name = BoonName(Offer.Boon);
			Choice.Description = Offer.Boon ? Offer.Boon->Description : FText::GetEmpty();
		}
		BoonSelectionWidget->SetTitle(FText::Format(NSLOCTEXT("FPSRL", "ChooseBlessing", "CHOOSE A {0} BLESSING"),
			FText::FromString(AspectName(Boons->ChosenAspect).ToString().ToUpper())));
		BoonSelectionWidget->SetReroll(false, FText::GetEmpty(), false);
		BoonSelectionWidget->SetBackVisible(true);
	}
	BoonSelectionWidget->OnBack.BindUObject(this, &ThisClass::HandleBoonBack);

	BoonSelectionWidget->SetChoices(Choices);
	BoonSelectionWidget->SetDeadline(0.0);	// altars have no timer
	BoonSelectionWidget->OnChoice.BindUObject(this, &ThisClass::HandleBoonChoice);
	BoonSelectionWidget->SetCloseVisible(true);
	BoonSelectionWidget->OnClose.BindUObject(this, &ThisClass::HandleBoonClose);
}

bool AFPSRLPlayerController::HasPendingBoonSelection() const
{
	const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>();
	const UFPSRLBoonComponent* Boons = PS ? PS->GetBoonComponent() : nullptr;
	if (!Boons)
	{
		return false;
	}
	if (Boons->PendingKind == EFPSRLBoonSelectionKind::Upgrade)
	{
		return !Boons->UpgradeOptions.IsEmpty();
	}
	if (Boons->PendingKind != EFPSRLBoonSelectionKind::Blessing)
	{
		return false;
	}
	// A Blessing altar is pending at whichever step it is on, once that step's choices have arrived.
	switch (Boons->AltarStep)
	{
	case EFPSRLAltarStep::ChooseAspect:	return !Boons->AspectOptions.IsEmpty();
	case EFPSRLAltarStep::ChooseSlot:	return !Boons->SlotOptions.IsEmpty();
	default:							return !Boons->CurrentOptions.IsEmpty();
	}
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
	// Only hides the screen; the choice stays pending on the server until picked.
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
		if (!IsAnyModalOpen() && !IsPauseMenuOpen())
		{
			SetInputMode(FInputModeGameOnly());
			SetShowMouseCursor(false);
		}
	}
}

void AFPSRLPlayerController::HandleBoonChoice(int32 OptionIndex)
{
	if (const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		const UFPSRLBoonComponent* Boons = PS->GetBoonComponent();
		if (Boons->PendingKind == EFPSRLBoonSelectionKind::Upgrade)
		{
			ServerSelectUpgrade(Boons->SelectionEventId, OptionIndex);
		}
		else if (Boons->AltarStep == EFPSRLAltarStep::ChooseAspect)
		{
			ServerChooseAspect(Boons->SelectionEventId, OptionIndex);
		}
		else if (Boons->AltarStep == EFPSRLAltarStep::ChooseSlot)
		{
			ServerChooseSlot(Boons->SelectionEventId, OptionIndex);
		}
		else
		{
			ServerSelectBoon(Boons->SelectionEventId, OptionIndex);
		}
	}
}

void AFPSRLPlayerController::HandleBoonBack()
{
	if (const AFPSRLPlayerState* PS = GetPlayerState<AFPSRLPlayerState>())
	{
		ServerAltarBack(PS->GetBoonComponent()->SelectionEventId);
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
	if (!Option->WeaponClass || !GiveWeaponToPawn(InPawn, Option->WeaponClass))
	{
		return;
	}

	// Weapons are local actors: clients see this and give the pawn the same weapon themselves.
	if (AFPSRLPlayerState* MutablePS = GetPlayerState<AFPSRLPlayerState>())
	{
		MutablePS->SetEquippedWeapon(Option->WeaponTag);
	}

	UE_LOG(LogFPSRL, Log, TEXT("Equipped %s on %s (%s)"), *Option->WeaponTag.ToString(), *InPawn->GetName(),
		PS ? *PS->GetPlayerName() : TEXT("no PlayerState"));
}

bool AFPSRLPlayerController::GiveWeaponToPawn(APawn* InPawn, UClass* WeaponClass)
{
	if (!InPawn || !WeaponClass)
	{
		return false;
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
		UE_LOG(LogFPSRL, Warning, TEXT("%s has no AddWeaponClass; cannot equip %s"), *InPawn->GetName(), *WeaponClass->GetName());
		return false;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca(AddWeaponFunction->ParmsSize));
	FMemory::Memzero(Params, AddWeaponFunction->ParmsSize);
	bool bParamSet = false;
	for (TFieldIterator<FProperty> It(AddWeaponFunction); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (const FClassProperty* ClassParam = CastField<FClassProperty>(*It))
		{
			if (WeaponClass->IsChildOf(ClassParam->MetaClass))
			{
				ClassParam->SetObjectPropertyValue_InContainer(Params, WeaponClass);
				bParamSet = true;
			}
			break;
		}
	}
	if (!bParamSet)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("AddWeaponClass on %s does not accept %s"), *InPawn->GetName(), *WeaponClass->GetName());
		return false;
	}
	InPawn->ProcessEvent(AddWeaponFunction, Params);
	return true;
}

// --- Combat ----------------------------------------------------------------------------------------------------------

void AFPSRLPlayerController::ServerFireProjectile_Implementation(TSubclassOf<AFPSRLProjectile> ProjectileClass, FVector_NetQuantize10 Location,
	FRotator Rotation, const TArray<FString>& SpawnSettings)
{
	APawn* Shooter = GetPawn();
	UWorld* World = GetWorld();
	if (!World || !Shooter || !ProjectileClass || ProjectileClass->HasAnyClassFlags(CLASS_Abstract) || !AFPSRLProjectile::CanPawnShoot(Shooter))
	{
		return;	// downed, dead, or nothing to fire
	}

	const double Now = World->GetTimeSeconds();
	if (LastServerShotTime >= 0.0 && Now - LastServerShotTime < MinClientShotInterval)
	{
		return;	// faster than any weapon fires
	}
	if (FVector::Dist(Location, Shooter->GetActorLocation()) > MaxClientShotOriginDistance)
	{
		return;	// not from where this player stands
	}
	LastServerShotTime = Now;

	AFPSRLProjectile* Projectile = World->SpawnActorDeferred<AFPSRLProjectile>(ProjectileClass, FTransform(Rotation, Location),
		Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->ImportSpawnSettings(SpawnSettings);
		Projectile->bHiddenFromInstigator = true;	// that client already shows its own copy
		Projectile->FinishSpawning(FTransform(Rotation, Location));
	}
}

void AFPSRLPlayerController::SetWeaponInputBlocked(bool bBlocked)
{
	if (!IsLocalController() || bBlocked == bWeaponInputBlocked)
	{
		return;
	}
	UInputMappingContext* Context = WeaponMappingContext.LoadSynchronous();
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Context || !Subsystem)
	{
		return;
	}

	if (bBlocked)
	{
		int32 Priority = 0;
		if (!Subsystem->HasMappingContext(Context, Priority))
		{
			return;	// not active, nothing to remove
		}
		BlockedWeaponContextPriority = Priority;
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = true;
		Subsystem->RemoveMappingContext(Context, Options);

		// Removing the mapping swallows the button release, so the weapon never hears "stop aiming" / "stop firing"
		// (downed while aiming stayed zoomed in for good). Tell it directly.
		if (APawn* CurrentPawn = GetPawn())
		{
			TArray<AActor*> Held;
			CurrentPawn->GetAttachedActors(Held, true, true);
			for (AActor* Weapon : Held)
			{
				for (const TCHAR* FunctionName : { TEXT("StopAiming"), TEXT("Stop Firing") })
				{
					UFunction* Function = Weapon ? Weapon->FindFunction(FunctionName) : nullptr;
					if (Function && Function->ParmsSize == 0)
					{
						Weapon->ProcessEvent(Function, nullptr);
					}
				}
			}
		}
	}
	else if (!Subsystem->HasMappingContext(Context))
	{
		Subsystem->AddMappingContext(Context, BlockedWeaponContextPriority);
	}
	bWeaponInputBlocked = bBlocked;
}

void AFPSRLPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	SetWeaponInputBlocked(false);	// a fresh pawn is never downed

	// Our own arms always animate (the PlayerState turns them off on other players' pawns; undo that if this pawn
	// replicated as someone else's before the possession arrived).
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(InPawn);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetFName() == TEXT("FirstPersonMesh"))
		{
			Mesh->SetComponentTickEnabled(true);
		}
	}
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
	for (UUserWidget* Widget : std::initializer_list<UUserWidget*>{ BoonSelectionWidget.Get(), PortalMenu.Get(), PortalStatus.Get(), DeathMenu.Get() })
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

// --- Expedition rooms --------------------------------------------------------------------------------------------------

void AFPSRLPlayerController::ServerReportRoomShown_Implementation(int32 PlacementIndex)
{
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->DepthLayout->ReportRoomShown(this, PlacementIndex);
	}
}

void AFPSRLPlayerController::RefreshEncounterBar(AFPSRLRoom* Room)
{
	if (!IsLocalController() || !Room)
	{
		return;
	}
	const UFPSRLEncounterDefinition* Encounter = Room->Encounter;
	UFPSRLHealthComponent* EnemyHealth = Room->EncounterEnemy ? Room->EncounterEnemy->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	const bool bShow = Encounter && Encounter->bShowHealthBar && Room->bCombatStarted && !Room->bRoomComplete && EnemyHealth;
	if (bShow)
	{
		if (!EncounterBar)
		{
			EncounterBar = CreateWidget<UFPSRLEncounterBarWidget>(this, EncounterBarClass ? EncounterBarClass : TSubclassOf<UFPSRLEncounterBarWidget>(UFPSRLEncounterBarWidget::StaticClass()));
		}
		if (EncounterBar)
		{
			EncounterBar->SetTarget(EnemyHealth, Encounter->DisplayName, Encounter->Kind);
			if (!EncounterBar->IsInViewport())
			{
				EncounterBar->AddToViewport(5);
			}
		}
		EncounterBarRoom = Room;
	}
	else if (EncounterBar && (!EncounterBarRoom.IsValid() || EncounterBarRoom == Room))
	{
		EncounterBar->ClearTarget();
		EncounterBar->RemoveFromParent();
		EncounterBarRoom.Reset();
	}
}
