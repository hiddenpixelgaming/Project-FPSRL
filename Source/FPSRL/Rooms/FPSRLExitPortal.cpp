// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLExitPortal.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "FPSRL.h"

namespace FPSRLPortalDebug
{
	static TAutoConsoleVariable<bool> CVarRunMessages(
		TEXT("FPSRL.Debug.RunMessages"),
		true,
		TEXT("Show on-screen messages for Depth completion (stand-in until a run HUD exists)."));

	static void Show(int32 Key, const FString& Message, const FColor& Color)
	{
		if (GEngine && CVarRunMessages.GetValueOnGameThread())
		{
			GEngine->AddOnScreenDebugMessage(Key, 5.f, Color, Message);
		}
	}
}

AFPSRLExitPortal::AFPSRLExitPortal()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PromptText = TEXT("Press E to use the Exit Portal");

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);	// walk-through
	Mesh->SetGenerateOverlapEvents(false);
	SetRootComponent(Mesh);

	InteractionRange = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionRange"));
	InteractionRange->SetupAttachment(Mesh);
	InteractionRange->SetUsingAbsoluteScale(true);
	InteractionRange->InitSphereRadius(250.f);
	InteractionRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionRange->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionRange->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionRange->SetGenerateOverlapEvents(true);
}

void AFPSRLExitPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLExitPortal, PortalState);
	DOREPLIFETIME(AFPSRLExitPortal, Destination);
	DOREPLIFETIME(AFPSRLExitPortal, ContinueVotes);
	DOREPLIFETIME(AFPSRLExitPortal, LivingPlayers);
	DOREPLIFETIME(AFPSRLExitPortal, CountdownEndTime);
}

void AFPSRLExitPortal::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		ApplyPortalState();
		return;
	}

	const UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
	Destination = RunSubsystem ? RunSubsystem->GetNextDestination() : EPortalDestination::RunComplete;

	PortalState = bVisibleWhileLocked ? EPortalState::Locked : EPortalState::Hidden;
	ApplyPortalState();

	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		PlayerLeftHandle = GameState->OnPlayerLeft.AddUObject(this, &ThisClass::HandlePlayerLeft);
		GameState->OnDepthCompleted.AddUniqueDynamic(this, &ThisClass::HandleDepthCompleted);
		if (GameState->bDepthComplete)
		{
			HandleDepthCompleted();
		}
	}
	else
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Portal %s] level has no AFPSRLGameState; portal disabled"), *GetActorNameOrLabel());
		SetPortalState(EPortalState::Disabled);
	}
}

void AFPSRLExitPortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ActivationTimer);
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	if (AFPSRLGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFPSRLGameState>() : nullptr)
	{
		GameState->OnPlayerLeft.Remove(PlayerLeftHandle);
	}
	Super::EndPlay(EndPlayReason);
}

// --- State ---------------------------------------------------------------------------------------------------------

void AFPSRLExitPortal::HandleDepthCompleted()
{
	if (PortalState != EPortalState::Hidden && PortalState != EPortalState::Locked)
	{
		return;
	}
	SetPortalState(EPortalState::Activating);

	const float Delay = UFPSRLRunSettings::Get().PortalActivationDelay;
	if (Delay > 0.f)
	{
		GetWorldTimerManager().SetTimer(ActivationTimer, this, &ThisClass::Activate, Delay, false);
	}
	else
	{
		Activate();
	}
}

void AFPSRLExitPortal::Activate()
{
	if (PortalState == EPortalState::Activating)
	{
		SetPortalState(EPortalState::Active);
		EvaluateVotes();	// fills LivingPlayers for the tracker
	}
}

void AFPSRLExitPortal::SetPortalState(EPortalState NewState)
{
	if (!HasAuthority() || PortalState == NewState)
	{
		return;
	}
	PortalState = NewState;
	UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] %s"), *GetActorNameOrLabel(), *UEnum::GetValueAsString(NewState));
	ForceNetUpdate();
	OnRep_PortalState();	// the listen-server host sees it too
}

void AFPSRLExitPortal::ApplyPortalState()
{
	const bool bVisible = PortalState != EPortalState::Hidden && PortalState != EPortalState::Disabled;
	SetActorHiddenInGame(!bVisible);
}

void AFPSRLExitPortal::OnRep_PortalState()
{
	ApplyPortalState();
	RefreshLocalInteractor();	// prompt only while Active (also for a player already standing here)
	K2_OnPortalStateChanged(PortalState);
	NotifyLocalPlayer();

	if (PortalState == EPortalState::Activating)
	{
		FPSRLPortalDebug::Show(7100, TEXT("DEPTH CLEARED - the exit portal is opening"), FColor::Green);
	}
}

// --- Vote ------------------------------------------------------------------------------------------------------------

void AFPSRLExitPortal::Interact_Implementation(APlayerController* User)
{
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(User); PC && CanInteract())
	{
		PC->OpenPortalMenu(this);
	}
}

bool AFPSRLExitPortal::IsLivingPlayer(const APlayerState* Player)
{
	// Dead or downed players don't vote and never block the party.
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	return Pawn && !Player->IsInactive() && UFPSRLHealthComponent::IsPawnUp(Pawn);
}

void AFPSRLExitPortal::SetContinueVote(APlayerController* Voter, bool bContinue)
{
	APlayerState* Player = Voter ? Voter->PlayerState : nullptr;
	if (!HasAuthority() || PortalState != EPortalState::Active || !Player)
	{
		return;
	}

	if (bContinue)
	{
		const APawn* Pawn = Voter->GetPawn();
		const float MaxDistance = InteractionRange->GetScaledSphereRadius() + 200.f;
		if (!IsLivingPlayer(Player) || !Pawn || FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) > MaxDistance)
		{
			UE_LOG(LogFPSRL, Warning, TEXT("[Portal %s] Continue from %s rejected (dead or out of range)"), *GetActorNameOrLabel(), *Player->GetPlayerName());
			return;
		}
		ContinueVotes.AddUnique(Player);
	}
	else
	{
		ContinueVotes.Remove(Player);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] %s chose %s"), *GetActorNameOrLabel(), *Player->GetPlayerName(), bContinue ? TEXT("Continue") : TEXT("Cancel"));
	EvaluateVotes();
}

void AFPSRLExitPortal::HandlePlayerLeft()
{
	EvaluateVotes();	// a leaver may complete the vote for the rest
}

void AFPSRLExitPortal::EvaluateVotes()
{
	if (!HasAuthority() || PortalState != EPortalState::Active)
	{
		return;
	}

	// Only living, connected players count, on both sides of the ratio.
	ContinueVotes.RemoveAll([](const TObjectPtr<APlayerState>& Player) { return !IsLivingPlayer(Player); });
	LivingPlayers = 0;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (const APlayerState* Player : GameState->PlayerArray)
		{
			LivingPlayers += IsLivingPlayer(Player) ? 1 : 0;
		}
	}

	const int32 Votes = ContinueVotes.Num();
	if (Votes > 0 && Votes >= LivingPlayers)
	{
		Travel();	// everyone (or the only player) wants to go
		return;
	}

	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	const bool bThresholdMet = LivingPlayers > 0 && Votes >= FMath::CeilToInt(LivingPlayers * Settings.PortalVoteThreshold);
	if (bThresholdMet && Settings.PortalCountdownSeconds > 0.f)
	{
		if (!GetWorldTimerManager().IsTimerActive(CountdownTimer))
		{
			GetWorldTimerManager().SetTimer(CountdownTimer, this, &ThisClass::Travel, Settings.PortalCountdownSeconds, false);
			CountdownEndTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + Settings.PortalCountdownSeconds;
			UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] %d/%d continuing: %.0fs countdown started"), *GetActorNameOrLabel(), Votes, LivingPlayers, Settings.PortalCountdownSeconds);
		}
	}
	else if (GetWorldTimerManager().IsTimerActive(CountdownTimer))
	{
		GetWorldTimerManager().ClearTimer(CountdownTimer);
		CountdownEndTime = 0.0;
		UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] below the threshold: countdown stopped"), *GetActorNameOrLabel());
	}

	ForceNetUpdate();
	OnRep_Votes();
}

void AFPSRLExitPortal::OnRep_Votes()
{
	K2_OnVotesChanged(ContinueVotes.Num(), LivingPlayers);
	NotifyLocalPlayer();
}

void AFPSRLExitPortal::NotifyLocalPlayer()
{
	if (AFPSRLPlayerController* PC = GetWorld() ? Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr)
	{
		PC->RefreshPortalUI(this);
	}
}

void AFPSRLExitPortal::Travel()
{
	if (!HasAuthority() || PortalState != EPortalState::Active)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	SetPortalState(EPortalState::Used);
	UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] travelling (%d/%d continuing, %s)"), *GetActorNameOrLabel(), ContinueVotes.Num(), LivingPlayers,
		*UEnum::GetValueAsString(Destination));

	if (UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		RunSubsystem->AdvanceRun(GetWorld());
	}
}
