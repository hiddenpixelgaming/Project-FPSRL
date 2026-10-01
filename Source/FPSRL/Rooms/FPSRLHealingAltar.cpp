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

	// A white health cross floating above the pillar: one bar along each axis, so it reads as a "+" from every side.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> White(TEXT("/Game/MainProject/Contents/Materials/Debug/MI_HealingAltar_White.MI_HealingAltar_White"));
	const FVector BarScales[] = { FVector(0.2f, 0.2f, 0.85f), FVector(0.85f, 0.2f, 0.2f), FVector(0.2f, 0.85f, 0.2f) };
	for (int32 Bar = 0; Bar < 3; ++Bar)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Cross%d"), Bar));
		Part->SetupAttachment(Mesh);
		Part->SetUsingAbsoluteScale(true);	// the pillar is stretched; the cross keeps its shape
		Part->SetRelativeScale3D(BarScales[Bar]);
		Part->SetRelativeLocation(FVector(0.f, 0.f, 125.f));	// x1.2 from the pillar: 1.5 m above its centre
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		if (Cube.Succeeded())
		{
			Part->SetStaticMesh(Cube.Object);
		}
		if (White.Succeeded())
		{
			Part->SetMaterial(0, White.Object);
		}
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
