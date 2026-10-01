// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLJumpPad.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BandMesh(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GlowMesh(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularGlow.SM_CircularGlow"));
	Band = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Band"));
	Band->SetupAttachment(Trigger);
	Band->SetRelativeLocation(FVector(0.f, 0.f, -40.f));
	Band->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Band->SetCanEverAffectNavigation(false);
	Band->SetStaticMesh(BandMesh.Object);
	Glow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Trigger);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, -40.f));
	Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Glow->SetCanEverAffectNavigation(false);
	Glow->SetStaticMesh(GlowMesh.Object);

	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Null::StaticClass());

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnTriggerBegin);
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
