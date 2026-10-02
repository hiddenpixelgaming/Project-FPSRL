// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLJumpPad.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "UObject/ConstructorHelpers.h"

// Falling braking of the players in flight from any pad (restored when they land; shared so chained pads keep the original).
static TMap<TWeakObjectPtr<ACharacter>, float> GJumpPadInFlight;

AFPSRLJumpPad::AFPSRLJumpPad()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetBoxExtent(FVector(90.f, 90.f, 40.f));
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(true);
	RootComponent = Trigger;

	// A bright cyan disc on the floor and an arrow along the launch direction (engine shapes: the template pad's glow
	// meshes rendered invisible in our arenas; playtest v0.1.32).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cyan(TEXT("/Game/MainProject/Contents/Materials/Debug/MI_JumpPad_Cyan.MI_JumpPad_Cyan"));
	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	Disc->SetupAttachment(Trigger);
	Disc->SetRelativeLocation(FVector(0.f, 0.f, 3.f));	// the pad origin (and trigger centre) is on the floor
	Disc->SetRelativeScale3D(FVector(1.8f, 1.8f, 0.06f));
	Arrow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Trigger);
	Arrow->SetRelativeScale3D(FVector(0.45f, 0.45f, 0.6f));
	for (UStaticMeshComponent* Part : { Disc.Get(), Arrow.Get() })
	{
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->SetMaterial(0, Cyan.Object);
	}
	Disc->SetStaticMesh(CylinderMesh.Object);
	Arrow->SetStaticMesh(ConeMesh.Object);
	PointArrow();

	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Null::StaticClass());

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnTriggerBegin);
}

void AFPSRLJumpPad::PointArrow()
{
	// The cone points along the launch (straight up for a vertical pad), standing on the disc.
	if (Arrow)
	{
		const FVector Direction = Velocity.IsNearlyZero() ? FVector::UpVector : Velocity.GetSafeNormal();
		Arrow->SetRelativeRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
		Arrow->SetRelativeLocation(FVector(0.f, 0.f, 6.f + 35.f));
	}
}

void AFPSRLJumpPad::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PointArrow();
}

void AFPSRLJumpPad::BeginPlay()
{
	Super::BeginPlay();
	PointArrow();
}

void AFPSRLJumpPad::OnTriggerBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (!Character || !Character->IsPlayerControlled())
	{
		return;	// players only
	}
	// The owning client launches itself at once (no correction lag); the server does the same for its copy.
	if (Character->IsLocallyControlled() || HasAuthority())
	{
		// The character's falling braking (template value) would eat the horizontal launch speed within a second, so the
		// flight is ballistic: no braking until it lands (air control input still steers).
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!GJumpPadInFlight.Contains(Character))
		{
			GJumpPadInFlight.Add(Character, Movement->BrakingDecelerationFalling);
			Character->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnLaunchedLanded);
		}
		Movement->BrakingDecelerationFalling = 0.f;
		Character->LaunchCharacter(Velocity, true, true);
	}
}

void AFPSRLJumpPad::OnLaunchedLanded(const FHitResult& Hit)
{
	for (auto It = GJumpPadInFlight.CreateIterator(); It; ++It)
	{
		ACharacter* Character = It.Key().Get();
		if (!Character)
		{
			It.RemoveCurrent();
		}
		else if (!Character->GetCharacterMovement()->IsFalling())
		{
			Character->GetCharacterMovement()->BrakingDecelerationFalling = It.Value();
			Character->LandedDelegate.RemoveDynamic(this, &ThisClass::OnLaunchedLanded);
			It.RemoveCurrent();
		}
	}
}
