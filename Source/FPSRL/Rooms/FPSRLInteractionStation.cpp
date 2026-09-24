// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLInteractionStation.h"
#include "GameFramework/Character.h"
#include "FPSRL.h"

AFPSRLInteractionStation::AFPSRLInteractionStation()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AFPSRLInteractionStation::Interact_Implementation(APlayerController* User)
{
}

ACharacter* AFPSRLInteractionStation::AsLocalPlayerCharacter(AActor* Actor)
{
	ACharacter* Character = Cast<ACharacter>(Actor);
	return (Character && Character->IsLocallyControlled() && Character->IsPlayerControlled()) ? Character : nullptr;
}

void AFPSRLInteractionStation::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	UE_LOG(LogFPSRL, Log, TEXT("[Station %s] BeginOverlap %s (local player: %d)"), *GetActorNameOrLabel(), *GetNameSafe(OtherActor), AsLocalPlayerCharacter(OtherActor) != nullptr);

	// Actor-level overlap begins on the first of this station's components the character touches, so it fires once.
	ACharacter* Character = AsLocalPlayerCharacter(OtherActor);
	if (Character && CanInteract() && !PromptedCharacter.IsValid())
	{
		PromptedCharacter = Character;
		K2_OnInteractorEntered(Character);
	}
}

void AFPSRLInteractionStation::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);

	UE_LOG(LogFPSRL, Log, TEXT("[Station %s] EndOverlap %s (local player: %d)"), *GetActorNameOrLabel(), *GetNameSafe(OtherActor), AsLocalPlayerCharacter(OtherActor) != nullptr);

	// ...and ends only when the character no longer touches any of them.
	ACharacter* Character = AsLocalPlayerCharacter(OtherActor);
	if (Character && PromptedCharacter.Get() == Character)
	{
		PromptedCharacter.Reset();
		K2_OnInteractorLeft(Character);
	}
}

void AFPSRLInteractionStation::RefreshLocalInteractor()
{
	if (!CanInteract())
	{
		if (ACharacter* Character = PromptedCharacter.Get())
		{
			PromptedCharacter.Reset();
			K2_OnInteractorLeft(Character);
		}
		return;
	}

	if (!PromptedCharacter.IsValid())
	{
		TArray<AActor*> Overlapping;
		GetOverlappingActors(Overlapping, ACharacter::StaticClass());
		for (AActor* Actor : Overlapping)
		{
			if (ACharacter* Character = AsLocalPlayerCharacter(Actor))
			{
				PromptedCharacter = Character;
				K2_OnInteractorEntered(Character);
				break;
			}
		}
	}
}
