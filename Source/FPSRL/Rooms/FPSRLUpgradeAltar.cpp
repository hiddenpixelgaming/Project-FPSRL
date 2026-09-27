// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLUpgradeAltar.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AFPSRLUpgradeAltar::AFPSRLUpgradeAltar()
{
	PromptText = TEXT("Press E to Upgrade a Blessing");

	// A placeholder look so the altar works without a Blueprint: a 1 m wide, 1.2 m tall pillar.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.2f));
	}
}

void AFPSRLUpgradeAltar::BeginPlay()
{
	Super::BeginPlay();

	// Static meshes can use the basic shape material (unlike skeletal meshes), so this tint also works when packaged.
	if (UMaterialInstanceDynamic* Tint = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Tint->SetVectorParameterValue(TEXT("Color"), PillarColor);
	}
}

bool AFPSRLUpgradeAltar::BeginPlayerSelection(AFPSRLPlayerState* Player)
{
	return Player && Player->GetBoonComponent()->BeginUpgradeSelection();
}
