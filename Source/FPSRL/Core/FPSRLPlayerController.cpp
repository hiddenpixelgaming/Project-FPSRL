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
#include "UI/FPSRLCombatHUDWidget.h"
#include "UI/FPSRLCatchUpWidget.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Rooms/FPSRLRoom.h"
#include "Data/FPSRLEncounterDefinition.h"
#include "Core/FPSRLAutopilotSubsystem.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "UI/FPSRLDeathMenuWidget.h"
#include "Components/FPSRLHealthComponent.h"
#include "Combat/FPSRLProjectile.h"
#include "Combat/FPSRLCombatRules.h"
#include "UI/FPSRLCombatFeedback.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputMappingContext.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerInput.h"
#include "Social/FPSRLChatSettings.h"
#include "Social/FPSRLChatSubsystem.h"
#include "UI/FPSRLChatWidget.h"
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
#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Types/FPSRLGameplayTags.h"
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

void AFPSRLPlayerController::FPSRLAutoRun(int32 TestFall, int32 MinPlayers)
{
	if (HasAuthority() && GetGameInstance())
	{
		GetGameInstance()->GetSubsystem<UFPSRLAutopilotSubsystem>()->Start(TestFall != 0, MinPlayers);
	}
}

void AFPSRLPlayerController::FPSRLAltarTest(int32 MinPlayers)
{
	if (HasAuthority() && GetGameInstance())
	{
		GetGameInstance()->GetSubsystem<UFPSRLAutopilotSubsystem>()->StartAltarTest(MinPlayers);
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
	else if (Command == TEXT("SpawnTestEnemy"))
	{
		// A stationary enemy right in front of this player (melee checks).
		UClass* EnemyClass = LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
		APawn* Me = GetPawn();
		if (EnemyClass && Me)
		{
			const float Distance = Arg1.IsEmpty() ? 120.f : FCString::Atof(*Arg1);
			const float Side = Arg2.IsEmpty() ? 0.f : FCString::Atof(*Arg2);
			const FTransform At(Me->GetActorRotation() + FRotator(0.f, 180.f, 0.f), Me->GetActorLocation() + Me->GetActorForwardVector() * Distance + Me->GetActorRightVector() * Side);
			if (APawn* Enemy = GetWorld()->SpawnActorDeferred<APawn>(EnemyClass, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn))
			{
				Enemy->AutoPossessAI = EAutoPossessAI::Disabled;
				Enemy->FinishSpawning(At);
				// Side "Miniboss" / "Boss": straight ahead, ranked like that encounter's enemy and already hurt (overhead bar checks).
				const FGameplayTag Rank = Arg2 == TEXT("Miniboss") ? FPSRLGameplayTags::Enemy_Rank_Miniboss : Arg2 == TEXT("Boss") ? FPSRLGameplayTags::Enemy_Rank_Boss : FGameplayTag();
				if (UAbilitySystemComponent* EnemyASC = Rank.IsValid() ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy) : nullptr)
				{
					EnemyASC->AddLooseGameplayTag(Rank, 1, EGameplayTagReplicationState::TagOnly);
					UGameplayStatics::ApplyDamage(Enemy, 10.f, this, GetPawn(), nullptr);
				}
				bDone = true;
			}
		}
	}
	else if (Command == TEXT("SpawnTestAI"))
	{
		// An enemy with its AI controller (Inactive until its encounter starts, like in a room), Distance ahead, Side right.
		UClass* EnemyClass = LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
		APawn* Me = GetPawn();
		if (EnemyClass && Me)
		{
			const float Distance = Arg1.IsEmpty() ? 1000.f : FCString::Atof(*Arg1);
			const float Side = Arg2.IsEmpty() ? 0.f : FCString::Atof(*Arg2);
			const FTransform At(Me->GetActorRotation() + FRotator(0.f, 180.f, 0.f), Me->GetActorLocation() + Me->GetActorForwardVector() * Distance + Me->GetActorRightVector() * Side);
			bDone = GetWorld()->SpawnActor<APawn>(EnemyClass, At, FActorSpawnParameters()) != nullptr;
		}
	}
	else if (Command == TEXT("Hurt"))
	{
		// Damage this player (downed / damage-flash checks).
		UFPSRLHealthComponent* Health = GetPawn() ? GetPawn()->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (Health)
		{
			Health->ApplyEnvironmentDamage(FCString::Atof(*Arg1));
			bDone = true;
		}
	}
	else if (Command == TEXT("God"))
	{
		PS->bGodMode = !PS->bGodMode;
		bDone = true;
		ClientShowNotice(PS->bGodMode ? NSLOCTEXT("FPSRL", "GodOn", "God mode ON") : NSLOCTEXT("FPSRL", "GodOff", "God mode OFF"));
	}
	else if (Command == TEXT("Heal"))
	{
		// Playtesting (F7): this player back to full health; a downed player is revived at full.
		if (UFPSRLHealthComponent* Health = GetPawn() ? GetPawn()->FindComponentByClass<UFPSRLHealthComponent>() : nullptr; Health && !Health->IsDead())
		{
			if (Health->IsDowned())
			{
				Health->Revive(1.f);
			}
			else
			{
				Health->Heal(Health->GetMaxHealth() - Health->GetCurrentHealth());
			}
			bDone = true;
			ClientShowNotice(NSLOCTEXT("FPSRL", "HealedToFull", "Healed to full"));
		}
	}
	else if (Command == TEXT("FastMove"))
	{
		// Playtesting (F9): the server's copy of this player's speed follows the client's.
		ApplyFastMove(Arg1 == TEXT("1"));
		bDone = true;
	}
	else if (Command == TEXT("KillEnemies"))
	{
		// Playtesting (F8): every living enemy dies (rooms clear normally, as if the players had won).
		int32 Killed = 0;
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			UFPSRLHealthComponent* Health = It->IsPlayerControlled() ? nullptr : It->FindComponentByClass<UFPSRLHealthComponent>();
			if (Health && !Health->IsDead())
			{
				Health->Kill();
				++Killed;
			}
		}
		bDone = true;
		ClientShowNotice(FText::Format(NSLOCTEXT("FPSRL", "KilledEnemies", "Killed {0} enemies"), Killed));
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

#if !UE_BUILD_SHIPPING
namespace FPSRLPortalDebug
{
	static TAutoConsoleVariable<int32> CVarAutoContinue(TEXT("fpsrl.Debug.PortalAutoContinue"), 0,
		TEXT("Test: players press the exit-portal prompt's Continue key as soon as it appears."));
}
#endif

void AFPSRLPlayerController::HandlePortalQuickContinue()
{
	const AFPSRLExitPortal* Portal = PortalStatus && PortalStatus->IsInViewport() ? PortalStatus->GetPortal() : nullptr;
	if (!IsLocalController() || !Portal || IsChatOpen() || Portal->HasVotedToContinue(PlayerState))
	{
		return;
	}
	ServerPortalVote(const_cast<AFPSRLExitPortal*>(Portal), true);
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
#if !UE_BUILD_SHIPPING
		// Test aid: press the prompt's [V] automatically (headless multiplayer runs).
		if (FPSRLPortalDebug::CVarAutoContinue.GetValueOnGameThread() != 0)
		{
			HandlePortalQuickContinue();
		}
#endif
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
	if (Altar->OpensSelectionScreen() && OpenBoonSelection())
	{
		return;	// a choice is already open: just show it again
	}
	// Show the screen as soon as the server's options replicate (RefreshBoonSelectionUI). An altar that acts at once
	// (Healing) has no screen.
	bBoonSelectionOpen = Altar->OpensSelectionScreen();
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
			It->RefreshAvailabilityTint();	// dark again once this player's choice there is done
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
			const EFPSRLBoonType Type = Offer.Boon ? Offer.Boon->BoonType : EFPSRLBoonType::Normal;
			const FText Kind = Type == EFPSRLBoonType::Major ? NSLOCTEXT("FPSRL", "MajorBlessing", "MAJOR BLESSING")
				: Type == EFPSRLBoonType::Minor ? NSLOCTEXT("FPSRL", "MinorBlessing", "MINOR BLESSING") : NSLOCTEXT("FPSRL", "NormalBlessing", "BLESSING");
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
	if (IsLocalController())
	{
		UInputMappingContext* ChatContext = UFPSRLChatSettings::Get()->ChatMappingContext.LoadSynchronous();
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
		if (ChatContext && Subsystem)
		{
			Subsystem->AddMappingContext(ChatContext, 100);	// the open-chat key (T, D-pad Left)
		}
	}

	// Listen host / cases where the PlayerState already exists (clients also get OnRep_PlayerState).
	BindToPlayerStateComponents();
	ReportTalentEssenceIfNeeded();

	EnsureLocalHUD();
}

void AFPSRLPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	EnsureLocalHUD();
}

void AFPSRLPlayerController::EnsureLocalHUD()
{
	if (!IsLocalController() || !GetLocalPlayer() || !HasActorBegunPlay())
	{
		return;	// before BeginPlay: the Blueprint's BeginPlay may still make its crosshair, BeginPlay calls this again
	}
	// Widgets we already have (a travel took them off the screen) go back instead of stacking new copies.
	if (!CombatHUD)
	{
		CombatHUD = CreateWidget<UFPSRLCombatHUDWidget>(this, CombatHUDClass ? CombatHUDClass : TSubclassOf<UFPSRLCombatHUDWidget>(UFPSRLCombatHUDWidget::StaticClass()));
	}
	if (CombatHUD && !CombatHUD->IsInViewport())
	{
		CombatHUD->AddToViewport(-1);	// under menus and prompts
		CombatHUD->SetController(this);
	}
	// Session chat: one widget per local player, reading the chat subsystem's history (which outlives the level).
	if (!ChatWidget)
	{
		ChatWidget = CreateWidget<UFPSRLChatWidget>(this, UFPSRLChatWidget::StaticClass());
	}
	if (ChatWidget && !ChatWidget->IsInViewport())
	{
		ChatWidget->AddToViewport(1);	// over the HUD, under menus
	}

	// The crosshair is the Blueprint's widget (CrosshairUI, typed WB_Crosshair): its BeginPlay only makes one in the
	// Lobby (v0.1.25: no crosshair in a run for anyone), so make it here when it is missing.
	if (const FObjectProperty* Property = FindFProperty<FObjectProperty>(GetClass(), TEXT("CrosshairUI")))
	{
		UUserWidget* Crosshair = Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(this));
		if (!Crosshair && Property->PropertyClass && Property->PropertyClass->IsChildOf(UUserWidget::StaticClass()))
		{
			Crosshair = CreateWidget<UUserWidget>(this, TSubclassOf<UUserWidget>(Property->PropertyClass));
			Property->SetObjectPropertyValue_InContainer(this, Crosshair);
		}
		if (Crosshair && !Crosshair->IsInViewport())
		{
			Crosshair->AddToViewport();
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
		if (UInputAction* Melee = MeleeAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(Melee, ETriggerEvent::Started, this, &ThisClass::HandleMeleePressed);
		}
		if (UInputAction* Dash = DashAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(Dash, ETriggerEvent::Started, this, &ThisClass::HandleDashPressed);
		}
		if (UInputAction* OpenChatAction = UFPSRLChatSettings::Get()->OpenChatAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(OpenChatAction, ETriggerEvent::Started, this, &ThisClass::OpenChat);
		}
	}
	if (InputComponent)
	{
		// The catch-up prompt's key (does nothing without an open offer; G is free in every mapping context). Not answering
		// is staying: there is no "stay" key.
		InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ThisClass::HandleCatchUpAccept);
		// The exit-portal prompt's button: join the Continue vote without walking back to the portal.
		InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ThisClass::HandlePortalQuickContinue);
	}
#if !UE_BUILD_SHIPPING
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ThisClass::FPSRLGod);	// F1-F5 are the engine's debug view modes
		InputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ThisClass::FPSRLHeal);
		InputComponent->BindKey(EKeys::F8, IE_Pressed, this, &ThisClass::FPSRLKillEnemies);
		InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ThisClass::FPSRLFastMove);
	}
#endif
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

// --- Catch-up (the server decides; this only shows the offer and sends the answer) -----------------------------------------

namespace FPSRLCatchUpDebug
{
	static TAutoConsoleVariable<int32> CVarAutoAnswer(TEXT("fpsrl.Debug.CatchUpAutoAnswer"), 0,
		TEXT("Testing, this machine: answer catch-up offers by themselves 1 s after they arrive. 0 = no (the player decides), ")
		TEXT("1 = catch up, 2 = stay, 3 = ask for the wrong room first (must be refused), then catch up, 4 = press G."));
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLProgressCommand(TEXT("fpsrl.Debug.Progress"),
	TEXT("Server: the expedition's active room and every player's room, state and catch-up offer ([Progress])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (const AFPSRLGameState* GameState = World ? World->GetGameState<AFPSRLGameState>() : nullptr; GameState && World->GetNetMode() != NM_Client)
		{
			const FString Text = GameState->DepthLayout->DescribeProgress();
			UE_LOG(LogFPSRL, Log, TEXT("[Progress] %s"), *Text);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, Text);
			}
		}
	}));
#endif

void AFPSRLPlayerController::ClientCatchUpOffer_Implementation(int32 RoomIndex, const FText& Message, const FText& RoomName)
{
	PendingCatchUpRoom = RoomIndex;
	if (!CatchUpWidget)
	{
		CatchUpWidget = CreateWidget<UFPSRLCatchUpWidget>(this, UFPSRLCatchUpWidget::StaticClass());
	}
	if (CatchUpWidget)
	{
		CatchUpWidget->ShowOffer(Message, RoomName);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] offer here: room %d: %s"), RoomIndex, *Message.ToString());

	const int32 AutoAnswer = FPSRLCatchUpDebug::CVarAutoAnswer.GetValueOnGameThread();
	if (AutoAnswer != 0)
	{
		GetWorldTimerManager().SetTimer(CatchUpAutoAnswerTimer, FTimerDelegate::CreateWeakLambda(this, [this, AutoAnswer, RoomIndex]()
		{
			if (AutoAnswer == 3)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] test: asking for room %d (not offered)"), RoomIndex + 2);
				ServerRequestCatchUp(RoomIndex + 2);
			}
			if (AutoAnswer == 4)
			{
				// Press the real key, through this player's input (checks the binding, not just the request).
				InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::G, IE_Pressed, 1.f));
				InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::G, IE_Released, 0.f));
				return;
			}
			AutoAnswer == 2 ? HandleCatchUpDecline() : HandleCatchUpAccept();
		}), 1.f, false);
	}
}

void AFPSRLPlayerController::ClientCatchUpResolved_Implementation(int32 RoomIndex, const FText& Message)
{
	if (RoomIndex != PendingCatchUpRoom)
	{
		// About another room (an older offer, or a refused request): the open offer, if any, stays up.
		if (!Message.IsEmpty())
		{
			ShowNotice(Message);
		}
		UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] answer about room %d here (open offer: %d): %s"), RoomIndex, PendingCatchUpRoom, *Message.ToString());
		return;
	}
	if (RoomIndex == PendingCatchUpRoom)
	{
		PendingCatchUpRoom = INDEX_NONE;
	}
	if (CatchUpWidget)
	{
		CatchUpWidget->HideOffer(Message);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] offer for room %d closed here%s%s"), RoomIndex, Message.IsEmpty() ? TEXT("") : TEXT(": "), *Message.ToString());
}

void AFPSRLPlayerController::HandleCatchUpAccept()
{
	if (PendingCatchUpRoom != INDEX_NONE)
	{
		ServerRequestCatchUp(PendingCatchUpRoom);	// the offer stays up until the server answers
	}
}

void AFPSRLPlayerController::HandleCatchUpDecline()
{
	if (PendingCatchUpRoom != INDEX_NONE)
	{
		ServerDeclineCatchUp(PendingCatchUpRoom);
	}
}

void AFPSRLPlayerController::ServerRequestCatchUp_Implementation(int32 RoomIndex)
{
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->DepthLayout->RequestCatchUp(this, RoomIndex);
	}
}

void AFPSRLPlayerController::ServerDeclineCatchUp_Implementation(int32 RoomIndex)
{
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->DepthLayout->DeclineCatchUp(this, RoomIndex);
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

void AFPSRLPlayerController::FPSRLGod()
{
	ServerTestCommand(TEXT("God"), FString(), FString());
}

void AFPSRLPlayerController::FPSRLHeal()
{
	ServerTestCommand(TEXT("Heal"), FString(), FString());
}

void AFPSRLPlayerController::FPSRLKillEnemies()
{
	ServerTestCommand(TEXT("KillEnemies"), FString(), FString());
}

void AFPSRLPlayerController::FPSRLFastMove()
{
	// Movement is predicted: the owning client and the server must both use the new speed.
	const bool bOn = FastMoveSavedSpeed < 0.f;
	ApplyFastMove(bOn);
	if (!HasAuthority())
	{
		ServerTestCommand(TEXT("FastMove"), bOn ? TEXT("1") : TEXT("0"), FString());
	}
	ShowNotice(bOn ? NSLOCTEXT("FPSRL", "FastMoveOn", "Fast move ON (x5)") : NSLOCTEXT("FPSRL", "FastMoveOff", "Fast move OFF"));
}

void AFPSRLPlayerController::ApplyFastMove(bool bOn)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = MyCharacter ? MyCharacter->GetCharacterMovement() : nullptr;
	if (!Movement || bOn == (FastMoveSavedSpeed >= 0.f))
	{
		return;
	}
	if (bOn)
	{
		FastMoveSavedSpeed = Movement->MaxWalkSpeed;
		Movement->MaxWalkSpeed = FastMoveSavedSpeed * 5.f;
	}
	else
	{
		Movement->MaxWalkSpeed = FastMoveSavedSpeed;
		FastMoveSavedSpeed = -1.f;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Test] fast move %s for %s: walk speed %.0f"), bOn ? TEXT("on") : TEXT("off"), *GetNameSafe(MyCharacter), Movement->MaxWalkSpeed);
}

void AFPSRLPlayerController::ClientShowNotice_Implementation(const FText& Message)
{
	ShowNotice(Message);
}

// --- Chat ----------------------------------------------------------------------------------------------------------

void AFPSRLPlayerController::ServerSendChatMessage_Implementation(const FString& Text)
{
	// Untrusted input: the subsystem validates everything and stamps sender, id and time itself. A refusal is only
	// reported back to this player; nothing else happens to them.
	UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this);
	const EFPSRLChatRejectReason Result = Chat ? Chat->ServerSubmit(this, Text) : EFPSRLChatRejectReason::NotInSession;
	if (Result != EFPSRLChatRejectReason::None)
	{
		ClientChatRejected(Result);
	}
}

void AFPSRLPlayerController::ClientReceiveChatMessage_Implementation(const FFPSRLChatMessage& Message)
{
	if (UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this))
	{
		Chat->ClientAddMessage(Message);
	}
}

void AFPSRLPlayerController::ClientReceiveChatHistory_Implementation(const TArray<FFPSRLChatMessage>& Messages)
{
	if (UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(this))
	{
		Chat->ClientMergeHistory(Messages);
	}
}

void AFPSRLPlayerController::ClientChatRejected_Implementation(EFPSRLChatRejectReason Reason)
{
	if (!ChatWidget)
	{
		return;
	}
	FText Notice;
	switch (Reason)
	{
	case EFPSRLChatRejectReason::RateLimited:	Notice = NSLOCTEXT("FPSRLChat", "RateLimited", "Slow down: wait a moment before sending again."); break;
	case EFPSRLChatRejectReason::TooLong:		Notice = NSLOCTEXT("FPSRLChat", "TooLong", "Message too long."); break;
	case EFPSRLChatRejectReason::Empty:			Notice = NSLOCTEXT("FPSRLChat", "Empty", "Nothing to send."); break;
	case EFPSRLChatRejectReason::Disabled:		Notice = NSLOCTEXT("FPSRLChat", "Disabled", "Chat is not available right now."); break;
	default:									Notice = NSLOCTEXT("FPSRLChat", "Refused", "Message not sent."); break;
	}
	ChatWidget->ShowNotice(Notice);
}

bool AFPSRLPlayerController::IsChatOpen() const
{
	return ChatWidget && ChatWidget->IsOpen();
}

void AFPSRLPlayerController::OpenChat()
{
	// Not over a screen that owns the input (altar choice, portal vote, pause menu); the death screen is fine.
	if (!IsLocalController() || !ChatWidget || ChatWidget->IsOpen() || IsPauseMenuOpen()
		|| (BoonSelectionWidget && BoonSelectionWidget->IsInViewport()) || (PortalMenu && PortalMenu->IsInViewport()))
	{
		return;
	}
	bChatRestoreCursor = bShowMouseCursor;
	ChatWidget->SetOpen(true);
	// Held keys (moving, firing) are released now: while the text box has the keyboard the game sees no key at all,
	// so typing can never move, shoot or open anything. The world, the AI and the network keep running.
	if (PlayerInput)
	{
		PlayerInput->FlushPressedKeys();
	}
	// Focus on the next frame, so the T that opened the chat is not typed into the box.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (ChatWidget && ChatWidget->IsOpen() && ChatWidget->GetInputBox())
		{
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(ChatWidget->GetInputBox()->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
			SetInputMode(Mode);
		}
	}));
	UE_LOG(LogFPSRL, Verbose, TEXT("[Chat] Opened"));
}

void AFPSRLPlayerController::CloseChat()
{
	if (!ChatWidget || !ChatWidget->IsOpen())
	{
		return;
	}
	ChatWidget->SetOpen(false);
	if (bChatRestoreCursor)
	{
		FInputModeGameAndUI Mode;	// a screen with a cursor (the death screen) was up: give it back
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Chat] Closed"));
}


void AFPSRLPlayerController::HandleMeleePressed()
{
	// The swing is done on the server for everyone: the host directly, a client by asking.
	if (!IsLocalController() || !GetPawn() || GetMeleeCooldownFraction() > 0.f)
	{
		return;	// still recovering from the last swing
	}
	LastLocalMeleeTime = GetWorld()->GetTimeSeconds();
	if (CombatHUD)
	{
		CombatHUD->NotifyMeleeSwung();
	}
	ServerMelee();	// on the listen host this runs right here
}

void AFPSRLPlayerController::ServerMelee_Implementation()
{
	APawn* Attacker = GetPawn();
	UWorld* World = GetWorld();
	if (!World || !Attacker || !AFPSRLProjectile::CanPawnShoot(Attacker))
	{
		return;	// downed or dead
	}
	const double Now = World->GetTimeSeconds();
	if (LastServerMeleeTime >= 0.0 && Now - LastServerMeleeTime < GetEffectiveMeleeCooldown() * 0.8f)
	{
		return;	// faster than anyone can swing
	}
	LastServerMeleeTime = Now;

	// The hit lands after a short windup: the swing is a commitment (an enemy can step out of reach meanwhile).
	if (MeleeWindup > 0.f)
	{
		GetWorldTimerManager().SetTimer(MeleeSwingTimer, this, &ThisClass::ResolveServerMelee, MeleeWindup, false);
	}
	else
	{
		ResolveServerMelee();
	}
}

void AFPSRLPlayerController::ResolveServerMelee()
{
	APawn* Attacker = GetPawn();
	if (!Attacker || !AFPSRLProjectile::CanPawnShoot(Attacker))
	{
		return;	// went down during the windup
	}
	// A MeleeRadius sphere along the facing, MeleeRange long (FPSRLCombat::MeleeSweep, shared with enemies). (The character
	// Blueprint's own swing traced on Visibility, which character bodies don't block, so it never damaged anyone; it still
	// runs, harmlessly.)
	const float Range = MeleeRange;
	const float Damage = MeleeDamage;
	// One swing = one attack (Event.Attack, hit or miss); every enemy it hits shares the attack id.
	CurrentMeleeAttackId = FPSRLCombat::NewAttackId();
	FPSRLCombat::NotifyAttack(Attacker, EFPSRLItemSource::Melee, CurrentMeleeAttackId);
	const TArray<AActor*> Struck = FPSRLCombat::MeleeSweep(Attacker, this, Range, MeleeRadius, Damage, MeleeMaxTargets);
	for (AActor* Victim : Struck)
	{
		// Impact: a push away from the player (spacing; the enemy's own movement takes it from there).
		if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim); VictimCharacter && MeleeKnockback > 0.f)
		{
			const FVector Away = (Victim->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
			VictimCharacter->LaunchCharacter(Away * MeleeKnockback + FVector(0.f, 0.f, MeleeKnockback * 0.3f), true, true);
		}
		UE_LOG(LogFPSRL, Log, TEXT("[Melee] %s hit %s for %.0f"), *(PlayerState ? PlayerState->GetPlayerName() : FString(TEXT("?"))), *Victim->GetName(), Damage);
	}
	if (Struck.IsEmpty())
	{
		ClientCombatFeedback(EFPSRLHitFeedback::MeleeMiss, 0.f, false, FVector::ZeroVector);	// the swing whiffed: tell the player clearly
		UE_LOG(LogFPSRL, Verbose, TEXT("[Melee] %s swung at nothing"), *(PlayerState ? PlayerState->GetPlayerName() : FString(TEXT("?"))));
	}
	CurrentMeleeAttackId = INDEX_NONE;
}

float AFPSRLPlayerController::GetMeleeCooldownFraction() const
{
	const float Cooldown = GetEffectiveMeleeCooldown();
	if (Cooldown <= 0.f || !GetWorld())
	{
		return 0.f;
	}
	const double Remaining = Cooldown - (GetWorld()->GetTimeSeconds() - LastLocalMeleeTime);
	return FMath::Clamp(static_cast<float>(Remaining / Cooldown), 0.f, 1.f);
}

void AFPSRLPlayerController::HandleDashPressed()
{
	if (CombatHUD)
	{
		CombatHUD->NotifyDashPressed();
	}
}

float AFPSRLPlayerController::GetEffectiveMeleeCooldown() const
{
	const AFPSRLPlayerState* State = GetPlayerState<AFPSRLPlayerState>();
	const UAbilitySystemComponent* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
	const float Multiplier = ASC && ASC->HasAttributeSetForAttribute(UFPSRLCombatSet::GetMeleeCooldownMultiplierAttribute())
		? ASC->GetNumericAttribute(UFPSRLCombatSet::GetMeleeCooldownMultiplierAttribute()) : 1.f;
	return MeleeCooldown * FMath::Max(0.1f, Multiplier);
}

void AFPSRLPlayerController::ClientCombatFeedback_Implementation(EFPSRLHitFeedback Kind, float Damage, bool bCritical, FVector_NetQuantize WorldLocation)
{
	if (CombatHUD)
	{
		CombatHUD->ShowHitFeedback(Kind);
		if (Kind != EFPSRLHitFeedback::MeleeMiss && Damage > 0.f)
		{
			CombatHUD->ShowDamageNumber(WorldLocation, Damage, bCritical);
		}
	}
	FPSRLCombatFeedback::PlaySound(this, Kind);
	if ((Kind == EFPSRLHitFeedback::MeleeHit || Kind == EFPSRLHitFeedback::MeleeKill) && PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraShake(UFPSRLMeleePunchShake::StaticClass(), 1.f);	// the swing connected
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Feedback] %s %.0f"), *StaticEnum<EFPSRLHitFeedback>()->GetNameStringByValue(static_cast<int64>(Kind)), Damage);
}

bool AFPSRLPlayerController::IsEnemyInMeleeReach() const
{
	// The same sweep the server swing uses (MeleeRange along the facing, MeleeRadius wide): what the brackets promise
	// is what the swing hits.
	const APawn* Me = GetPawn();
	UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return false;
	}
	const FVector Start = Me->GetActorLocation();
	const FVector End = Start + Me->GetActorForwardVector() * MeleeRange;
	TArray<FHitResult> Hits;
	UKismetSystemLibrary::SphereTraceMultiForObjects(const_cast<AFPSRLPlayerController*>(this), Start, End, MeleeRadius, { UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		false, { const_cast<APawn*>(Me) }, EDrawDebugTrace::None, Hits, true);
	for (const FHitResult& Hit : Hits)
	{
		const AActor* Other = Hit.GetActor();
		const UFPSRLHealthComponent* Health = Other ? Other->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (Health && !Health->IsDead() && Health->GetCurrentHealth() > 0.f && !UFPSRLHealthComponent::IsPlayerSide(nullptr, Other))
		{
			return true;
		}
	}
	return false;
}
