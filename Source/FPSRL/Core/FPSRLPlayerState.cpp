// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerState.h"
#include "Abilities/FPSRLAbilitySystemComponent.h"
#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Abilities/Attributes/FPSRLProgressionSet.h"
#include "Components/FPSRLAspectComponent.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

AFPSRLPlayerState::AFPSRLPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UFPSRLAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Attribute sets that are default subobjects of the ASC's owner are registered with the ASC automatically.
	HealthSet = CreateDefaultSubobject<UFPSRLHealthSet>(TEXT("HealthSet"));
	CombatSet = CreateDefaultSubobject<UFPSRLCombatSet>(TEXT("CombatSet"));
	ProgressionSet = CreateDefaultSubobject<UFPSRLProgressionSet>(TEXT("ProgressionSet"));

	BoonComponent = CreateDefaultSubobject<UFPSRLBoonComponent>(TEXT("BoonComponent"));
	AspectComponent = CreateDefaultSubobject<UFPSRLAspectComponent>(TEXT("AspectComponent"));

	// PlayerStates default to a very low update rate; GAS state (tags, attributes) needs to reach clients promptly.
	SetNetUpdateFrequency(100.f);

	SelectedWeapon = FPSRLGameplayTags::Weapon_Rifle;
}

UAbilitySystemComponent* AFPSRLPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AFPSRLPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLPlayerState, bIsReady);
	DOREPLIFETIME(AFPSRLPlayerState, SelectedWeapon);
	DOREPLIFETIME_CONDITION(AFPSRLPlayerState, TalentEssence, COND_OwnerOnly);
}

void AFPSRLPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Called on the server during seamless travel (old PlayerState -> new one) and on reconnect.
	// The run build (weapon, aspect, boons) travels Depth to Depth; the Lobby clears it (ClearRunState).
	if (AFPSRLPlayerState* NewState = Cast<AFPSRLPlayerState>(PlayerState))
	{
		NewState->SelectedWeapon = SelectedWeapon;
		NewState->TalentEssence = TalentEssence;
		NewState->bTalentEssenceReported = bTalentEssenceReported;
		NewState->bRunStateActive = bRunStateActive;
		AspectComponent->CopyChoiceTo(NewState->AspectComponent);
		BoonComponent->CopyRunStateTo(NewState->BoonComponent);
	}
}

void AFPSRLPlayerState::SetIsReady(bool bNewReady)
{
	if (HasAuthority())
	{
		bIsReady = bNewReady;
		ForceNetUpdate();
	}
}

void AFPSRLPlayerState::SetSelectedWeapon(const FGameplayTag& NewWeapon)
{
	if (HasAuthority())
	{
		SelectedWeapon = NewWeapon;
		ForceNetUpdate();
	}
}

bool AFPSRLPlayerState::HasCompletedLoadout() const
{
	return SelectedWeapon.IsValid() && AspectComponent && AspectComponent->HasCompletedAspectChoice();
}

// --- Run state ---------------------------------------------------------------------------------------------------

void AFPSRLPlayerState::BeginRunState()
{
	if (HasAuthority())
	{
		bRunStateActive = true;
		AspectComponent->ApplyActiveAspect();
		BoonComponent->RestoreRunState();	// boons carried from the previous Depth (no-op on the first)
	}
}

void AFPSRLPlayerState::ClearRunState()
{
	if (!HasAuthority() || !bRunStateActive)
	{
		return;	// nothing to clear, or already cleared
	}
	bRunStateActive = false;

	BoonComponent->ClearRunState();
	AspectComponent->ClearRunState();

	// Safety net for anything a run system granted without a tracked handle. Only temporary run effects.
	const int32 Swept = AbilitySystemComponent->RemoveActiveEffectsWithTags(FGameplayTagContainer(FPSRLGameplayTags::Effect_Temporary_Run));
	UE_LOG(LogFPSRL, Log, TEXT("%s run state cleared (%d untracked temporary effect(s) swept)"), *GetPlayerName(), Swept);
}

// --- Currency ----------------------------------------------------------------------------------------------------

void AFPSRLPlayerState::ReceiveReportedTalentEssence(int32 Amount)
{
	if (HasAuthority() && !bTalentEssenceReported)
	{
		bTalentEssenceReported = true;
		TalentEssence = FMath::Max(0, Amount);
		ForceNetUpdate();
	}
}

bool AFPSRLPlayerState::TrySpendTalentEssence(int32 Amount)
{
	if (!HasAuthority() || Amount < 0 || TalentEssence < Amount)
	{
		return false;
	}
	TalentEssence -= Amount;
	PersistTalentEssence();
	return true;
}

void AFPSRLPlayerState::AddTalentEssence(int32 Amount)
{
	if (HasAuthority() && Amount > 0)
	{
		TalentEssence += Amount;
		PersistTalentEssence();
	}
}

void AFPSRLPlayerState::PersistTalentEssence()
{
	ForceNetUpdate();
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(GetPlayerController()))
	{
		PC->ClientPersistentCurrencyChanged(TalentEssence);
	}
}

// --- Pawn / ASC --------------------------------------------------------------------------------------------------

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
