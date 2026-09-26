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
	DOREPLIFETIME(AFPSRLPlayerState, EquippedWeapon);
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

		// Health is never restored between Depths, only in the Lobby. A value still waiting to be applied (no pawn
		// yet) is passed on as is.
		if (bRunStateActive)
		{
			NewState->CarriedHealth = CarriedHealth >= 0.f ? CarriedHealth
				: (AbilitySystemComponent->GetSet<UFPSRLHealthSet>() ? AbilitySystemComponent->GetNumericAttribute(UFPSRLHealthSet::GetHealthAttribute()) : -1.f);
		}
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

void AFPSRLPlayerState::SetEquippedWeapon(const FGameplayTag& NewWeapon)
{
	if (HasAuthority())
	{
		EquippedWeapon = NewWeapon;
		ForceNetUpdate();
	}
}

void AFPSRLPlayerState::OnRep_EquippedWeapon()
{
	EquipWeaponLocally();
}

void AFPSRLPlayerState::EquipWeaponLocally()
{
	UWorld* World = GetWorld();
	APawn* CurrentPawn = GetPawn();
	if (HasAuthority() || !World || !CurrentPawn || !EquippedWeapon.IsValid())
	{
		return;	// the server equips through AFPSRLPlayerController::EquipSelectedWeapon
	}
	if (LocallyEquippedPawn == CurrentPawn && LocallyEquippedWeapon == EquippedWeapon)
	{
		return;	// already done
	}
	if (!CurrentPawn->HasActorBegunPlay())
	{
		// Pawn replicated but not initialized yet: its BeginPlay sets up the meshes the weapon attaches to.
		World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::EquipWeaponLocally);
		return;
	}

	// The weapon list lives on the controller class (same on every machine); use this machine's own controller.
	const AFPSRLPlayerController* LocalPC = Cast<AFPSRLPlayerController>(World->GetFirstPlayerController());
	const FFPSRLWeaponOption* Option = LocalPC ? LocalPC->FindWeaponOption(EquippedWeapon) : nullptr;
	if (!Option || !Option->WeaponClass)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Client] no weapon class for %s (%s)"), *EquippedWeapon.ToString(), *GetPlayerName());
		return;
	}
	if (AFPSRLPlayerController::GiveWeaponToPawn(CurrentPawn, Option->WeaponClass))
	{
		LocallyEquippedPawn = CurrentPawn;
		LocallyEquippedWeapon = EquippedWeapon;
		UE_LOG(LogFPSRL, Log, TEXT("[Client] equipped %s on %s (%s)"), *EquippedWeapon.ToString(), *CurrentPawn->GetName(), *GetPlayerName());
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

		// In a run, lethal damage downs this player (revivable by a teammate) instead of killing them outright.
		if (!AbilitySystemComponent->HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downable))
		{
			AbilitySystemComponent->AddLooseGameplayTag(FPSRLGameplayTags::Status_Downable, 1, EGameplayTagReplicationState::TagOnly);
		}

		AspectComponent->ApplyActiveAspect();
		BoonComponent->RestoreRunState();	// boons carried from the previous Depth (no-op on the first)

		// Spawning reset health to full; put back what the player had when they left the previous Depth (after the
		// boons, so a raised MaxHealth is in place). Never below 1, so nobody arrives dead.
		if (CarriedHealth >= 0.f && AbilitySystemComponent->GetSet<UFPSRLHealthSet>())
		{
			const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(UFPSRLHealthSet::GetMaxHealthAttribute());
			const float Health = FMath::Clamp(CarriedHealth, 1.f, MaxHealth);
			AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), Health);
			UE_LOG(LogFPSRL, Log, TEXT("%s: health carried over (%.0f / %.0f)"), *GetPlayerName(), Health, MaxHealth);
			CarriedHealth = -1.f;
		}
	}
}

void AFPSRLPlayerState::ClearRunState()
{
	if (!HasAuthority() || !bRunStateActive)
	{
		return;	// nothing to clear, or already cleared
	}
	bRunStateActive = false;
	CarriedHealth = -1.f;	// back in the Lobby: the spawn's full health stands
	AbilitySystemComponent->SetLooseGameplayTagCount(FPSRLGameplayTags::Status_Downable, 0, EGameplayTagReplicationState::TagOnly);

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

		EquipWeaponLocally();	// client: the pawn may arrive after EquippedWeapon did
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
