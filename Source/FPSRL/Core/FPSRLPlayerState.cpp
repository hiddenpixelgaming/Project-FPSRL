// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerState.h"
#include "Abilities/FPSRLAbilitySystemComponent.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Components/FPSRLHealthComponent.h"
#include "GameFramework/Pawn.h"
#include "FPSRL.h"

AFPSRLPlayerState::AFPSRLPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UFPSRLAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Attribute sets that are default subobjects of the ASC's owner are registered with the ASC automatically.
	HealthSet = CreateDefaultSubobject<UFPSRLHealthSet>(TEXT("HealthSet"));

	// PlayerStates default to a very low update rate; GAS state (tags, attributes) needs to reach clients promptly.
	SetNetUpdateFrequency(100.f);
}

UAbilitySystemComponent* AFPSRLPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AFPSRLPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	OnPawnSet.AddDynamic(this, &ThisClass::HandlePawnSet);
}

void AFPSRLPlayerState::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (OldPawn)
	{
		if (UFPSRLHealthComponent* OldHealth = OldPawn->FindComponentByClass<UFPSRLHealthComponent>())
		{
			OldHealth->UninitializeFromAbilitySystem();
		}
	}

	if (NewPawn)
	{
		// Owner = this PlayerState (keeps the data), Avatar = the pawn (the body abilities act through).
		AbilitySystemComponent->InitAbilityActorInfo(this, NewPawn);

		if (UFPSRLHealthComponent* NewHealth = NewPawn->FindComponentByClass<UFPSRLHealthComponent>())
		{
			NewHealth->InitializeWithAbilitySystem(AbilitySystemComponent);
		}
	}
	else if (AbilitySystemComponent->GetAvatarActor() == OldPawn)
	{
		// Pawn died or was unpossessed: stop anything running through it, keep granted effects on the PlayerState.
		AbilitySystemComponent->CancelAllAbilities();
		AbilitySystemComponent->RemoveAllGameplayCues();
		AbilitySystemComponent->SetAvatarActor(nullptr);
	}

	UE_LOG(LogFPSRL, Log, TEXT("[%s] ASC avatar for %s set to %s"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"),
		*GetPlayerName(),
		*GetNameSafe(AbilitySystemComponent->GetAvatarActor()));
}
