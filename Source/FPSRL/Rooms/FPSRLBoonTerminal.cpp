// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLBoonTerminal.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLRoom.h"
#include "FPSRL.h"

AFPSRLBoonTerminal::AFPSRLBoonTerminal()
{
	bReplicates = true;
	PromptText = TEXT("Press E to Choose a Blessing");
	UpgradeLookMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MainProject/Contents/Materials/Debug/MI_UpgradeAltar_Gold.MI_UpgradeAltar_Gold")));

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
	DOREPLIFETIME(AFPSRLBoonTerminal, ClaimedBy);
}

void AFPSRLBoonTerminal::BeginPlay()
{
	Super::BeginPlay();

	OriginalMaterial = Mesh->GetMaterial(0);
	OriginalPromptText = PromptText;
	RefreshLocalAppearance();	// a player who arrives with every Blessing taken sees an Upgrade Altar
	RefreshAvailabilityTint();	// dark until its room is cleared

	K2_OnUnlockedChanged(bUnlocked);
	if (!HasAuthority())
	{
		return;
	}

	if (!Room)
	{
		SetUnlocked(true);	// no encounter to wait for
	}
	else if (Room->IsRoomComplete())
	{
		HandleRoomCompleted();
	}
	else
	{
		Room->OnRoomCompleted.AddUniqueDynamic(this, &ThisClass::HandleRoomCompleted);
	}
}

void AFPSRLBoonTerminal::HandleRoomCompleted()
{
	SetUnlocked(true);
}

void AFPSRLBoonTerminal::SetUnlocked(bool bNewUnlocked)
{
	if (!HasAuthority() || bUnlocked == bNewUnlocked)
	{
		return;
	}
	bUnlocked = bNewUnlocked;
	UE_LOG(LogFPSRL, Log, TEXT("[Boons] %s %s"), *GetActorNameOrLabel(), bUnlocked ? TEXT("unlocked") : TEXT("locked"));
	ForceNetUpdate();
	OnRep_Unlocked();	// the listen-server host is a local player too
}

void AFPSRLBoonTerminal::OnRep_Unlocked()
{
	RefreshLocalInteractor();
	RefreshAvailabilityTint();
	K2_OnUnlockedChanged(bUnlocked);
}

void AFPSRLBoonTerminal::OnRep_ClaimedBy()
{
	RefreshLocalInteractor();
	RefreshAvailabilityTint();
}

void AFPSRLBoonTerminal::RefreshAvailabilityTint()
{
	if (GetNetMode() == NM_DedicatedServer || !Mesh)
	{
		return;
	}
	// A new instance whenever the mesh's material changed (first time, or the Upgrade look swapped it).
	UMaterialInterface* Current = Mesh->GetMaterial(0);
	if (!Current)
	{
		return;
	}
	if (Current != TintMaterial)
	{
		TintMaterial = Mesh->CreateDynamicMaterialInstance(0, Current);
		bTintHasColor = TintMaterial && TintMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), TintBaseColor);
	}
	if (TintMaterial && bTintHasColor)
	{
		FLinearColor Shown = CanInteract() ? TintBaseColor : TintBaseColor * 0.05f;	// much darker: not usable right now
		Shown.A = TintBaseColor.A;
		TintMaterial->SetVectorParameterValue(TEXT("Color"), Shown);
	}
}

bool AFPSRLBoonTerminal::HasBeenUsedBy(const APlayerState* Player) const
{
	return Player && ClaimedBy.Contains(Player);
}

bool AFPSRLBoonTerminal::CanInteract() const
{
	if (!bUnlocked)
	{
		return false;	// room not cleared yet
	}
	const UWorld* World = GetWorld();
	const AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !HasBeenUsedBy(PC->PlayerState))
	{
		return true;
	}
	return PC->HasPendingBoonSelection();	// used, but the choice is still open: allow reopening it
}

void AFPSRLBoonTerminal::Interact_Implementation(APlayerController* User)
{
	if (!CanInteract())
	{
		return;
	}
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(User))
	{
		PC->UseBoonAltar(this);
	}
}

bool AFPSRLBoonTerminal::TryOffer(AFPSRLPlayerController* PC, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!HasAuthority() || !PS || !Pawn)
	{
		return false;
	}
	if (!bUnlocked)
	{
		OutReason = NSLOCTEXT("FPSRL", "AltarLocked", "Clear the room first");
		return false;
	}
	if (HasBeenUsedBy(PS))
	{
		OutReason = NSLOCTEXT("FPSRL", "AltarUsed", "You have already used this altar");
		return false;
	}

	// The client is trusted to be at the altar only within a margin of the prompt radius.
	const float MaxDistance = InteractionRange->GetScaledSphereRadius() + 150.f;
	if (FVector::Dist(Pawn->GetActorLocation(), InteractionRange->GetComponentLocation()) > MaxDistance)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Boons] %s: %s is out of range"), *GetActorNameOrLabel(), *PS->GetPlayerName());
		return false;
	}

	if (!BeginPlayerSelection(PS))
	{
		// A choice already open elsewhere reopens on the client; otherwise there is truly nothing to pick or upgrade.
		if (!PS->GetBoonComponent()->HasPendingSelection())
		{
			OutReason = GetNothingToOfferText();
		}
		return false;
	}
	ClaimedBy.Add(PS);
	ForceNetUpdate();
	OnRep_ClaimedBy();
	return true;
}

bool AFPSRLBoonTerminal::BeginPlayerSelection(AFPSRLPlayerState* Player)
{
	return Player && Player->GetBoonComponent()->BeginAltar();	// Blessings, or upgrades once every Blessing is taken
}

void AFPSRLBoonTerminal::RefreshLocalAppearance()
{
	// Purely local: other players may still have Blessings to pick at this same altar.
	const UWorld* World = GetWorld();
	const AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	const bool bUpgradeLook = PS && PS->GetBoonComponent()->bAllBlessingsChosen;
	if (bUpgradeLook == bShowingUpgradeLook)
	{
		return;
	}
	bShowingUpgradeLook = bUpgradeLook;

	UMaterialInterface* UpgradeMaterial = bUpgradeLook ? UpgradeLookMaterial.LoadSynchronous() : nullptr;
	Mesh->SetMaterial(0, bUpgradeLook && UpgradeMaterial ? UpgradeMaterial : OriginalMaterial.Get());
	PromptText = bUpgradeLook ? UpgradePromptText : OriginalPromptText;
	RefreshAvailabilityTint();
}
