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

namespace
{
	/**
	 * Calls a Blueprint function on the character by name, filling its parameters by type: object parameters get the
	 * station (when compatible), string/text parameters get the prompt. Bridge until the character is C++ (Step F).
	 */
	void CallInteractorFunction(ACharacter* Interactor, const FName FunctionName, AFPSRLInteractionStation* Station)
	{
		UFunction* Function = Interactor ? Interactor->FindFunction(FunctionName) : nullptr;
		if (!Function)
		{
			UE_LOG(LogFPSRL, Warning, TEXT("[Station %s] %s has no '%s'; no interact prompt"), *Station->GetActorNameOrLabel(),
				*GetNameSafe(Interactor), *FunctionName.ToString());
			return;
		}

		uint8* Params = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
			if (It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				continue;
			}
			if (const FObjectPropertyBase* ObjectParam = CastField<FObjectPropertyBase>(*It))
			{
				if (Station->IsA(ObjectParam->PropertyClass))
				{
					ObjectParam->SetObjectPropertyValue_InContainer(Params, Station);
				}
			}
			else if (const FStrProperty* StringParam = CastField<FStrProperty>(*It))
			{
				StringParam->SetPropertyValue_InContainer(Params, Station->PromptText);
			}
			else if (const FTextProperty* TextParam = CastField<FTextProperty>(*It))
			{
				TextParam->SetPropertyValue_InContainer(Params, FText::FromString(Station->PromptText));
			}
		}
		Interactor->ProcessEvent(Function, Params);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
	}
}

void AFPSRLInteractionStation::K2_OnInteractorEntered_Implementation(ACharacter* Interactor)
{
	CallInteractorFunction(Interactor, TEXT("SetCurrentInteractable"), this);
}

void AFPSRLInteractionStation::K2_OnInteractorLeft_Implementation(ACharacter* Interactor)
{
	CallInteractorFunction(Interactor, TEXT("ClearCurrentInteractable"), this);
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
