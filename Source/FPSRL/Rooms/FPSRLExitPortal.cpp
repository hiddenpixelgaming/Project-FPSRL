// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLExitPortal.h"
#include "Components/BoxComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLGameState.h"
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
		TEXT("Show on-screen messages for Depth completion and the exit portal (stand-in until a run HUD exists)."));

	static void Show(int32 Key, const FString& Message, const FColor& Color = FColor::Cyan)
	{
		if (GEngine && CVarRunMessages.GetValueOnGameThread())
		{
			GEngine->AddOnScreenDebugMessage(Key, 5.f, Color, Message);
		}
	}
}

AFPSRLExitPortal::AFPSRLExitPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);	// walk-through
	Mesh->SetGenerateOverlapEvents(false);
	SetRootComponent(Mesh);

	EntryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryZone"));
	EntryZone->SetupAttachment(Mesh);
	EntryZone->SetUsingAbsoluteScale(true);
	EntryZone->InitBoxExtent(FVector(120.f, 120.f, 150.f));
	EntryZone->SetCollisionObjectType(ECC_WorldDynamic);
	EntryZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);	// enabled while Active
	EntryZone->SetGenerateOverlapEvents(true);
}

void AFPSRLExitPortal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLExitPortal, PortalState);
	DOREPLIFETIME(AFPSRLExitPortal, Destination);
	DOREPLIFETIME(AFPSRLExitPortal, ReadyPlayers);
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

	EntryZone->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnEntryBeginOverlap);
	EntryZone->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::OnEntryEndOverlap);

	const UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
	Destination = RunSubsystem ? RunSubsystem->GetNextDestination() : EPortalDestination::RunComplete;

	PortalState = bVisibleWhileLocked ? EPortalState::Locked : EPortalState::Hidden;
	ApplyPortalState();

	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
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
		RefreshReadyPlayers();	// players already standing where the portal opened
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
	// Only the server decides who is ready; clients never need the overlap.
	EntryZone->SetCollisionEnabled(HasAuthority() && PortalState == EPortalState::Active
		? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AFPSRLExitPortal::OnRep_PortalState()
{
	ApplyPortalState();
	K2_OnPortalStateChanged(PortalState);

	switch (PortalState)
	{
	case EPortalState::Activating:
		FPSRLPortalDebug::Show(7100, TEXT("DEPTH CLEARED"), FColor::Green);
		break;
	case EPortalState::Active:
		FPSRLPortalDebug::Show(7101, Destination == EPortalDestination::RunComplete
			? TEXT("EXIT PORTAL OPEN - leads out of the run") : TEXT("EXIT PORTAL OPEN - step in when you are ready"), FColor::Green);
		break;
	default:
		break;
	}
}

// --- Ready tracking -----------------------------------------------------------------------------------------------

APlayerState* AFPSRLExitPortal::GetLivingPlayerState(AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return nullptr;
	}
	const UFPSRLHealthComponent* Health = Pawn->FindComponentByClass<UFPSRLHealthComponent>();
	return (Health && Health->IsDead()) ? nullptr : Pawn->GetPlayerState();
}

void AFPSRLExitPortal::OnEntryBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	RefreshReadyPlayers();
}

void AFPSRLExitPortal::OnEntryEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	RefreshReadyPlayers();
}

void AFPSRLExitPortal::RefreshReadyPlayers()
{
	if (!HasAuthority() || PortalState != EPortalState::Active)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	EntryZone->GetOverlappingActors(Overlapping, APawn::StaticClass());
	ReadyPlayers.Reset();
	for (AActor* Actor : Overlapping)
	{
		if (APlayerState* Player = GetLivingPlayerState(Actor))
		{
			ReadyPlayers.AddUnique(Player);
		}
	}
	EvaluateReady();
}

void AFPSRLExitPortal::EvaluateReady()
{
	LivingPlayers = 0;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* Player : GameState->PlayerArray)
		{
			if (Player && !Player->IsInactive() && GetLivingPlayerState(Player->GetPawn()))
			{
				++LivingPlayers;
			}
		}
	}

	const float CountdownSeconds = UFPSRLRunSettings::Get().PortalCountdownSeconds;
	if (!ReadyPlayers.IsEmpty() && ReadyPlayers.Num() >= LivingPlayers)
	{
		Travel();	// everyone who can come is here
		return;
	}
	if (ReadyPlayers.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(CountdownTimer);
		CountdownEndTime = 0.0;
	}
	else if (CountdownSeconds > 0.f && !GetWorldTimerManager().IsTimerActive(CountdownTimer))
	{
		GetWorldTimerManager().SetTimer(CountdownTimer, this, &ThisClass::Travel, CountdownSeconds, false);
		CountdownEndTime = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + CountdownSeconds;
	}
	ForceNetUpdate();
	OnRep_ReadyPlayers();
}

void AFPSRLExitPortal::OnRep_ReadyPlayers()
{
	K2_OnReadyPlayersChanged(ReadyPlayers.Num(), LivingPlayers);

	if (PortalState != EPortalState::Active || ReadyPlayers.IsEmpty())
	{
		return;
	}
	FString Message = FString::Printf(TEXT("Portal: %d / %d players ready"), ReadyPlayers.Num(), LivingPlayers);
	if (CountdownEndTime > 0.0)
	{
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		const int32 Seconds = GameState ? FMath::Max(0, FMath::CeilToInt(CountdownEndTime - GameState->GetServerWorldTimeSeconds())) : 0;
		Message += FString::Printf(TEXT(" - everyone leaves in %ds"), Seconds);
	}
	FPSRLPortalDebug::Show(7102, Message, FColor::Yellow);
}

void AFPSRLExitPortal::Travel()
{
	if (!HasAuthority() || PortalState != EPortalState::Active)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(CountdownTimer);
	SetPortalState(EPortalState::Used);
	UE_LOG(LogFPSRL, Log, TEXT("[Portal %s] travelling (%d/%d ready, %s)"), *GetActorNameOrLabel(), ReadyPlayers.Num(), LivingPlayers,
		*UEnum::GetValueAsString(Destination));

	if (UFPSRLRunSubsystem* RunSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		RunSubsystem->AdvanceRun(GetWorld());
	}
}
