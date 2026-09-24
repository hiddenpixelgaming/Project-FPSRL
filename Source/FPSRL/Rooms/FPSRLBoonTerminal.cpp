// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLBoonTerminal.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/CollisionProfile.h"
#include "Net/UnrealNetwork.h"
#include "FPSRL.h"

AFPSRLBoonTerminal::AFPSRLBoonTerminal()
{
	bReplicates = true;
	PromptText = TEXT("Press E to Choose a Boon");

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	SetRootComponent(Mesh);

	InteractionRange = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionRange"));
	InteractionRange->SetupAttachment(Mesh);
	InteractionRange->InitSphereRadius(200.f);
	InteractionRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionRange->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionRange->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionRange->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionRange->SetGenerateOverlapEvents(true);
}

void AFPSRLBoonTerminal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSRLBoonTerminal, bUnlocked);
}

void AFPSRLBoonTerminal::BeginPlay()
{
	Super::BeginPlay();

	K2_OnUnlockedChanged(bUnlocked);
	if (!HasAuthority())
	{
		return;
	}

	FMulticastDelegateProperty* ClearedEvent = Arena ? FindFProperty<FMulticastDelegateProperty>(Arena->GetClass(), ArenaCompleteEvent) : nullptr;
	if (!ClearedEvent)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Boons] %s: no Arena linked (or it has no '%s' event); this terminal will stay locked"),
			*GetActorNameOrLabel(), *ArenaCompleteEvent.ToString());
		return;
	}

	FScriptDelegate Delegate;
	Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(ThisClass, HandleArenaCleared));
	ClearedEvent->AddDelegate(MoveTemp(Delegate), Arena);
}

void AFPSRLBoonTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AFPSRLGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFPSRLGameState>() : nullptr)
	{
		GameState->OnBoonSelectionComplete.RemoveDynamic(this, &ThisClass::HandleSelectionComplete);
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSRLBoonTerminal::HandleArenaCleared()
{
	AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>();
	if (!HasAuthority() || !GameState)
	{
		return;
	}

	// The arena starts the selection before announcing completion. If it is already over (nobody had anything to
	// choose from), there is nothing to unlock for.
	if (!GameState->bBoonSelectionActive)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s: room cleared but no Boon selection is running; staying locked"), *GetActorNameOrLabel());
		return;
	}

	GameState->OnBoonSelectionComplete.AddUniqueDynamic(this, &ThisClass::HandleSelectionComplete);
	SetUnlocked(true);
}

void AFPSRLBoonTerminal::HandleSelectionComplete()
{
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		GameState->OnBoonSelectionComplete.RemoveDynamic(this, &ThisClass::HandleSelectionComplete);
	}
	SetUnlocked(false);
}

void AFPSRLBoonTerminal::SetUnlocked(bool bNewUnlocked)
{
	if (!HasAuthority() || bUnlocked == bNewUnlocked)
	{
		return;
	}
	bUnlocked = bNewUnlocked;
	UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s %s"), *GetActorNameOrLabel(), bUnlocked ? TEXT("unlocked (room cleared)") : TEXT("locked"));
	ForceNetUpdate();
	OnRep_Unlocked();	// the listen-server host is a local player too
}

void AFPSRLBoonTerminal::OnRep_Unlocked()
{
	RefreshLocalInteractor();
	K2_OnUnlockedChanged(bUnlocked);
}

void AFPSRLBoonTerminal::Interact_Implementation(APlayerController* User)
{
	if (!CanInteract())
	{
		return;	// room not cleared yet
	}

	AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(User);
	if (!PC || !PC->OpenBoonSelection())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s: no pending Boon choice for %s"), *GetActorNameOrLabel(), *GetNameSafe(User));
	}
}
