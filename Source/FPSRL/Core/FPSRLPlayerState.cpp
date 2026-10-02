// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerState.h"
#include "Abilities/FPSRLAbilitySystemComponent.h"
#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Abilities/Attributes/FPSRLProgressionSet.h"
#include "Components/FPSRLRelicComponent.h"
#include "Data/FPSRLBoonSettings.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
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
	RelicComponent = CreateDefaultSubobject<UFPSRLRelicComponent>(TEXT("RelicComponent"));

	// PlayerStates default to a very low update rate; GAS state (tags, attributes) needs to reach clients promptly.
	SetNetUpdateFrequency(100.f);

	SelectedWeapon = FPSRLGameplayTags::Weapon_Rifle;
	SecondaryItem = UFPSRLBoonSettings::Get().DefaultSecondaryItem;
	AbilityItem = UFPSRLBoonSettings::Get().DefaultAbilityItem;
}

UAbilitySystemComponent* AFPSRLPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AFPSRLPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSRLPlayerState, bIsReady);
	DOREPLIFETIME(AFPSRLPlayerState, ExpeditionRoomIndex);
	DOREPLIFETIME(AFPSRLPlayerState, bGodMode);
	DOREPLIFETIME(AFPSRLPlayerState, SelectedWeapon);
	DOREPLIFETIME(AFPSRLPlayerState, EquippedWeapon);
	DOREPLIFETIME(AFPSRLPlayerState, SecondaryItem);
	DOREPLIFETIME(AFPSRLPlayerState, AbilityItem);
	DOREPLIFETIME_CONDITION(AFPSRLPlayerState, TalentEssence, COND_OwnerOnly);
}

void AFPSRLPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Called on the server during seamless travel (old PlayerState -> new one) and on reconnect.
	// The run build (weapon, Blessings, relics) travels Depth to Depth; the Lobby clears it (ClearRunState).
	if (AFPSRLPlayerState* NewState = Cast<AFPSRLPlayerState>(PlayerState))
	{
		NewState->SelectedWeapon = SelectedWeapon;
		NewState->bGodMode = bGodMode;
		NewState->SecondaryItem = SecondaryItem;
		NewState->AbilityItem = AbilityItem;
		NewState->TalentEssence = TalentEssence;
		NewState->bTalentEssenceReported = bTalentEssenceReported;
		NewState->bRunStateActive = bRunStateActive;
		BoonComponent->CopyRunStateTo(NewState->BoonComponent);
		RelicComponent->CopyRunStateTo(NewState->RelicComponent);

		// Health is never restored between Depths, only in the Lobby. A value still waiting to be applied (no pawn
		// yet) is passed on as is.
		if (bRunStateActive)
		{
			NewState->CarriedHealth = CarriedHealth >= 0.f ? CarriedHealth
				: (AbilitySystemComponent->GetSet<UFPSRLHealthSet>() ? AbilitySystemComponent->GetNumericAttribute(UFPSRLHealthSet::GetHealthAttribute()) : -1.f);
		}
		OnPlayerStateChanged.Broadcast(NewState);	// it has the player's id and name now (the host's teammate HUD)
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
		StopFirstPersonAnimationIfRemote(CurrentPawn);	// equipping switches the arms to ABP_FP_Weapon
	}
}

bool AFPSRLPlayerState::HasCompletedLoadout() const
{
	return SelectedWeapon.IsValid();
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

		BoonComponent->RestoreRunState();	// Blessings and relics carried from the previous Depth (no-op on the first)
		RelicComponent->RestoreRunState();

		// Spawning reset health to full; put back what the player had when they left the previous Depth (after the
		// Blessings, so a raised MaxHealth is in place). Never below 1, so nobody arrives dead.
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
	RelicComponent->ClearRunState();

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

// --- Session roster events (teammate HUD) ------------------------------------------------------------------------

FFPSRLPlayerStateEvent AFPSRLPlayerState::OnPlayerStateBegin;
FFPSRLPlayerStateEndEvent AFPSRLPlayerState::OnPlayerStateEnd;
FFPSRLPlayerStateEvent AFPSRLPlayerState::OnPlayerStateChanged;

void AFPSRLPlayerState::BeginPlay()
{
	Super::BeginPlay();
	OnPlayerStateBegin.Broadcast(this);
}

void AFPSRLPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnPlayerStateEnd.Broadcast(this, EndPlayReason);
	Super::EndPlay(EndPlayReason);
}

void AFPSRLPlayerState::SetPlayerName(const FString& S)
{
	Super::SetPlayerName(S);
	OnPlayerStateChanged.Broadcast(this);	// the server's own copy (a listen host's HUD) gets no OnRep
}

void AFPSRLPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();
	OnPlayerStateChanged.Broadcast(this);
}

void AFPSRLPlayerState::OnRep_bIsInactive()
{
	Super::OnRep_bIsInactive();
	OnPlayerStateChanged.Broadcast(this);
}

// --- Pawn / ASC --------------------------------------------------------------------------------------------------

void AFPSRLPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	OnPawnSet.AddDynamic(this, &ThisClass::HandlePawnSet);
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UFPSRLCombatSet::GetMoveSpeedMultiplierAttribute()).AddUObject(this, &ThisClass::HandleMoveSpeedChanged);
}

void AFPSRLPlayerState::StopFirstPersonAnimationIfRemote(APawn* InPawn) const
{
	// Someone else's pawn on a client. Its controller never replicates here, so the template's ABP_FP_Weapon (on the
	// first-person arms) failed GetController every frame: ~300 "Accessed None" log lines a second. Those arms are drawn
	// only for their own player, so nobody here needs them animated. Decided by net role rather than by comparing with
	// the local controller: right after joining or travelling the local controller's PlayerState isn't there yet
	// (v0.1.2 log: that check bailed out and the spam continued).
	if (!InPawn || InPawn->GetNetMode() != NM_Client || InPawn->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(InPawn);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetFName() == TEXT("FirstPersonMesh"))
		{
			Mesh->SetComponentTickEnabled(false);
		}
	}
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
		ApplyMoveSpeed();
		StopFirstPersonAnimationIfRemote(NewPawn);
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

void AFPSRLPlayerState::ApplyMoveSpeed()
{
	ACharacter* Character = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || AbilitySystemComponent->HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downed))
	{
		return;
	}
	const ACharacter* Defaults = Character->GetClass()->GetDefaultObject<ACharacter>();
	const float BaseSpeed = Defaults && Defaults->GetCharacterMovement() ? Defaults->GetCharacterMovement()->MaxWalkSpeed : Movement->MaxWalkSpeed;
	Movement->MaxWalkSpeed = BaseSpeed * AbilitySystemComponent->GetNumericAttribute(UFPSRLCombatSet::GetMoveSpeedMultiplierAttribute());
}
