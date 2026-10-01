// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLHealingAltar.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "FPSRL.h"

AFPSRLHealingAltar::AFPSRLHealingAltar()
{
	PromptText = TEXT("Press E to Heal");

	// Placeholder look (like the Upgrade Altar's gold pillar): a lime green pillar, a saved material instance so it is
	// cooked into packaged builds.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Lime(TEXT("/Game/MainProject/Contents/Materials/Debug/MI_HealingAltar_Lime.MI_HealingAltar_Lime"));
	if (Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.2f));
	}
	if (Lime.Succeeded())
	{
		Mesh->SetMaterial(0, Lime.Object);
	}
}

bool AFPSRLHealingAltar::BeginPlayerSelection(AFPSRLPlayerState* Player)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	if (!Health || Health->IsDead() || Health->IsDowned() || Health->GetCurrentHealth() >= Health->GetMaxHealth())
	{
		return false;	// nothing to heal: the player keeps their use
	}
	const float Missing = Health->GetMaxHealth() - Health->GetCurrentHealth();
	Health->Heal(Missing);
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Player->GetPlayerController()))
	{
		PC->ClientShowNotice(NSLOCTEXT("FPSRL", "AltarHealed", "Healed to full"));
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Altar] %s healed %s by %.0f to full"), *GetActorNameOrLabel(), *Player->GetPlayerName(), Missing);
	return true;
}

FText AFPSRLHealingAltar::GetNothingToOfferText() const
{
	return NSLOCTEXT("FPSRL", "AlreadyFullHealth", "You are already at full health");
}
