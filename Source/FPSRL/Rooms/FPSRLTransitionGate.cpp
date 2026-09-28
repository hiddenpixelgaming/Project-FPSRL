// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLTransitionGate.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLGameState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "FPSRL.h"

AFPSRLTransitionGate::AFPSRLTransitionGate()
{
	PrimaryActorTick.bCanEverTick = false;

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Blocker->SetGenerateOverlapEvents(false);
	SetRootComponent(Blocker);

	Barrier = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Barrier"));
	Barrier->SetupAttachment(Blocker);
	Barrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Barrier->SetStaticMesh(Cube.Object);
	}
}

void AFPSRLTransitionGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Blocker->SetBoxExtent(GateSize * 0.5f);
	Barrier->SetRelativeScale3D(GateSize / 100.f);	// the engine cube is 100 units
}

void AFPSRLTransitionGate::BeginPlay()
{
	Super::BeginPlay();
	SetOpen(false);
	if (AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>())
	{
		ReadinessHandle = GameState->DepthLayout->OnReadinessChanged.AddUObject(this, &ThisClass::Refresh);
	}
	Refresh();
}

void AFPSRLTransitionGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AFPSRLGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AFPSRLGameState>() : nullptr)
	{
		GameState->DepthLayout->OnReadinessChanged.Remove(ReadinessHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSRLTransitionGate::Refresh()
{
	// Open once the room after this traversal is loaded everywhere. Outside a generated Depth (a handcrafted test map)
	// there is nothing to wait for.
	const AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>();
	const UFPSRLDepthLayoutComponent* Layout = GameState ? GameState->DepthLayout.Get() : nullptr;
	if (!Layout || !Layout->HasLayout())
	{
		SetOpen(true);
		return;
	}
	const int32 Index = Layout->FindPlacementIndex(this);
	SetOpen(Index == INDEX_NONE || Index + 1 >= Layout->Placements.Num() || Layout->ReadyThrough >= Index + 1);
}

void AFPSRLTransitionGate::SetOpen(bool bNewOpen)
{
	if (bOpen == bNewOpen && HasActorBegunPlay())
	{
		return;
	}
	bOpen = bNewOpen;
	Blocker->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	Barrier->SetVisibility(!bOpen);
	UE_LOG(LogFPSRL, Verbose, TEXT("[Gate %s] %s"), *GetActorNameOrLabel(), bOpen ? TEXT("open") : TEXT("closed"));
}
