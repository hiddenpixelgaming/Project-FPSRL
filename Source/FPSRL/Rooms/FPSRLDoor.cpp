// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLDoor.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "FPSRL.h"

AFPSRLDoor::AFPSRLDoor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);

	DoorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));
	RootComponent = DoorRoot;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(DoorRoot);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DoorMesh->SetGenerateOverlapEvents(false);

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(DoorRoot);
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetGenerateOverlapEvents(false);
	Blocker->SetCanEverAffectNavigation(false);

	DoorwayZone = CreateDefaultSubobject<UBoxComponent>(TEXT("DoorwayZone"));
	DoorwayZone->SetupAttachment(DoorRoot);
	DoorwayZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorwayZone->SetCollisionObjectType(ECC_WorldDynamic);
	DoorwayZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	DoorwayZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DoorwayZone->SetGenerateOverlapEvents(true);
	DoorwayZone->SetCanEverAffectNavigation(false);

	InsideDirection = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("InsideDirection"));
	if (InsideDirection)
	{
		InsideDirection->SetupAttachment(DoorRoot);
		InsideDirection->ArrowColor = FColor::Green;
		InsideDirection->bIsScreenSizeScaled = false;
	}

	UpdateGeometry();
}

void AFPSRLDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLDoor, DoorState);
	DOREPLIFETIME(AFPSRLDoor, CrossedPlayers);
}

void AFPSRLDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateGeometry();
}

void AFPSRLDoor::BeginPlay()
{
	Super::BeginPlay();

	DoorwayZone->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnDoorwayBeginOverlap);
	DoorwayZone->OnComponentEndOverlap.AddDynamic(this, &ThisClass::OnDoorwayEndOverlap);

	ApplyDoorState();
}

void AFPSRLDoor::UpdateGeometry()
{
	const float HalfHeight = DoorHeight * 0.5f;
	const FVector Center(0.f, 0.f, HalfHeight);

	Blocker->SetBoxExtent(FVector(WallThickness * 0.5f, DoorWidth * 0.5f, HalfHeight));
	Blocker->SetRelativeLocation(Center);

	DoorwayZone->SetBoxExtent(FVector(WallThickness * 0.5f + ZoneDepth, DoorWidth * 0.5f, HalfHeight));
	DoorwayZone->SetRelativeLocation(Center);

	if (bScaleMeshToDoorSize)
	{
		// The engine cube is 100 cm per side.
		DoorMesh->SetRelativeLocation(Center);
		DoorMesh->SetRelativeScale3D(FVector(WallThickness, DoorWidth, DoorHeight) / 100.f);
	}

	if (InsideDirection)
	{
		InsideDirection->SetRelativeLocation(Center);
		InsideDirection->ArrowLength = ZoneDepth + WallThickness;
	}
}

// --- State ---------------------------------------------------------------------------------------------------

void AFPSRLDoor::Open()
{
	if (DoorState != EDoorState::Locked)
	{
		SetDoorState(EDoorState::Open);
	}
}

void AFPSRLDoor::Close()
{
	if (DoorState != EDoorState::Locked)
	{
		SetDoorState(EDoorState::Closed);
	}
}

void AFPSRLDoor::Lock()
{
	SetDoorState(EDoorState::Locked);
}

void AFPSRLDoor::Unlock()
{
	if (DoorState == EDoorState::Locked)
	{
		SetDoorState(EDoorState::Open);
	}
}

void AFPSRLDoor::SetDoorState(EDoorState NewState)
{
	if (!HasAuthority() || DoorState == NewState)
	{
		return;
	}

	DoorState = NewState;
	ApplyDoorState();
	ForceNetUpdate();
}

void AFPSRLDoor::OnRep_DoorState()
{
	ApplyDoorState();
}

void AFPSRLDoor::ApplyDoorState()
{
	// A one-way entrance keeps its wall solid even when Open; passage is granted per player instead.
	const bool bSolid = DoorState != EDoorState::Open || bIsOneWayEntrance;
	Blocker->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	if (DoorState != EDoorState::Open)
	{
		RevokeAllPassage();
	}

	K2_OnDoorStateChanged(DoorState);
}

bool AFPSRLDoor::HasPlayerCrossed(const APlayerState* PlayerState) const
{
	return PlayerState && CrossedPlayers.Contains(PlayerState);
}

// --- One-way passage -----------------------------------------------------------------------------------------

void AFPSRLDoor::OnDoorwayBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bIsOneWayEntrance || DoorState != EDoorState::Open)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (!Character || OtherComp != Character->GetCapsuleComponent())
	{
		return;
	}

	// Only players; AI has no PlayerState and is always blocked.
	if (const APlayerState* PlayerState = Character->GetPlayerState(); PlayerState && !HasPlayerCrossed(PlayerState))
	{
		SetPassage(Character, true);
	}
}

void AFPSRLDoor::OnDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (!Character || OtherComp != Character->GetCapsuleComponent() || !CharactersWithPassage.Contains(Character))
	{
		return;
	}

	SetPassage(Character, false);

	// Which side did they leave on? +X is inside.
	const FVector LocalPosition = GetActorTransform().InverseTransformPosition(Character->GetActorLocation());
	if (LocalPosition.X > 0.f)
	{
		if (APlayerState* PlayerState = Character->GetPlayerState())
		{
			// Server's list is authoritative and replicates; the owning client records it too so it doesn't
			// re-grant passage in the moment before replication arrives.
			CrossedPlayers.AddUnique(PlayerState);

			if (HasAuthority())
			{
				UE_LOG(LogFPSRL, Log, TEXT("%s: %s crossed the one-way entrance"), *GetActorNameOrLabel(), *PlayerState->GetPlayerName());
			}
		}
	}
}

void AFPSRLDoor::SetPassage(ACharacter* Character, bool bAllow)
{
	if (UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr)
	{
		Capsule->IgnoreComponentWhenMoving(Blocker, bAllow);
	}

	if (bAllow)
	{
		CharactersWithPassage.AddUnique(Character);
	}
	else
	{
		CharactersWithPassage.Remove(Character);
	}
}

void AFPSRLDoor::RevokeAllPassage()
{
	for (int32 Index = CharactersWithPassage.Num() - 1; Index >= 0; --Index)
	{
		if (ACharacter* Character = CharactersWithPassage[Index].Get())
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->IgnoreComponentWhenMoving(Blocker, false);
			}
		}
	}
	CharactersWithPassage.Reset();
}
