// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLUpgradeAltar.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AFPSRLUpgradeAltar::AFPSRLUpgradeAltar()
{
	PromptText = TEXT("Press E to Upgrade a Blessing");

	// A placeholder look so the altar works without a Blueprint: a 1 m wide, 1.2 m tall gold pillar. The gold is a saved
	// material instance (like the Blessing altar's red), so it is cooked into packaged builds; a runtime tint of the
	// engine material showed grey in v0.1.5.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Gold(TEXT("/Game/MainProject/Contents/Materials/Debug/MI_UpgradeAltar_Gold.MI_UpgradeAltar_Gold"));
	if (Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.2f));
	}
	if (Gold.Succeeded())
	{
		Mesh->SetMaterial(0, Gold.Object);
	}
}

bool AFPSRLUpgradeAltar::BeginPlayerSelection(AFPSRLPlayerState* Player)
{
	return Player && Player->GetBoonComponent()->BeginUpgradeSelection();
}
