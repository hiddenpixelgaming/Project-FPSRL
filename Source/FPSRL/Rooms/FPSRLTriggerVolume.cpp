// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLTriggerVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "FPSRL.h"

AFPSRLTriggerVolume::AFPSRLTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(false);

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	RootComponent = TriggerBox;
	TriggerBox->SetBoxExtent(FVector(300.f, 50.f, 100.f));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->SetCanEverAffectNavigation(false);
	TriggerBox->ShapeColor = FColor::Orange;
}

void AFPSRLTriggerVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSRLTriggerVolume, bHasFired);
}

void AFPSRLTriggerVolume::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnBoxBeginOverlap);
	}
}

void AFPSRLTriggerVolume::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bFireOnce && bHasFired)
	{
		return;
	}

	// Players only: a character whose capsule entered and which is controlled by a player.
	const ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (!Character || OtherComp != Character->GetCapsuleComponent() || !Character->IsPlayerControlled())
	{
		return;
	}

	bHasFired = true;
	UE_LOG(LogFPSRL, Log, TEXT("%s triggered by %s"), *GetActorNameOrLabel(), *OtherActor->GetActorNameOrLabel());
	OnTriggered.Broadcast(OtherActor);
}

void AFPSRLTriggerVolume::ResetTrigger()
{
	if (HasAuthority())
	{
		bHasFired = false;
	}
}
