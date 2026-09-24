// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLHealthComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Abilities/Effects/FPSRLHealthEffects.h"
#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

namespace FPSRLHealthDebug
{
	static TAutoConsoleVariable<bool> CVarHealthMessages(
		TEXT("FPSRL.Debug.HealthMessages"),
		true,
		TEXT("Show on-screen messages when anything takes damage or dies (stand-in until a health HUD exists)."));

	static void Show(const FString& Message, const FColor& Color)
	{
		UE_LOG(LogFPSRL, Log, TEXT("%s"), *Message);
#if !UE_BUILD_SHIPPING
		if (GEngine && CVarHealthMessages.GetValueOnGameThread())
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.f, Color, Message);
		}
#endif
	}
}

UFPSRLHealthComponent::UFPSRLHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFPSRLHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Enemies carry their own ASC. Players are initialized by AFPSRLPlayerState instead (their pawn has no ASC).
	if (!AbilitySystemComponent)
	{
		if (UAbilitySystemComponent* OwnerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
		{
			InitializeWithAbilitySystem(OwnerASC);
		}
	}
}

void UFPSRLHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeFromAbilitySystem();
	Super::EndPlay(EndPlayReason);
}

void UFPSRLHealthComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InASC)
{
	if (!InASC || InASC == AbilitySystemComponent)
	{
		return;
	}

	UninitializeFromAbilitySystem();
	AbilitySystemComponent = InASC;

	if (GetOwner()->HasAuthority())
	{
		const UFPSRLHealthSet* HealthSet = InASC->GetSet<UFPSRLHealthSet>();
		if (!HealthSet)
		{
			// Server adds the set; it replicates to clients as a subobject of the ASC's owner.
			HealthSet = InASC->AddAttributeSetSubobject(NewObject<UFPSRLHealthSet>(InASC->GetOwner()));
		}

		// Fresh spawn: full health. (A player's set outlives the pawn, so this also resets it on respawn.)
		InASC->SetNumericAttributeBase(UFPSRLHealthSet::GetMaxHealthAttribute(), DefaultMaxHealth);
		InASC->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), DefaultMaxHealth);

		HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
	}

	InASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthAttributeChanged);
	InASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthAttributeChanged);

	// On a client the set may not have replicated yet; its first OnRep will broadcast instead.
	if (GetHealthSet())
	{
		OnHealthChanged.Broadcast(GetCurrentHealth(), GetMaxHealth());
	}
}

void UFPSRLHealthComponent::UninitializeFromAbilitySystem()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetHealthAttribute()).RemoveAll(this);
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetMaxHealthAttribute()).RemoveAll(this);
	if (const UFPSRLHealthSet* HealthSet = GetHealthSet())
	{
		HealthSet->OnOutOfHealth.RemoveAll(this);
	}

	AbilitySystemComponent = nullptr;
}

const UFPSRLHealthSet* UFPSRLHealthComponent::GetHealthSet() const
{
	return AbilitySystemComponent ? AbilitySystemComponent->GetSet<UFPSRLHealthSet>() : nullptr;
}

void UFPSRLHealthComponent::HandleTakeAnyDamage(float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (Damage <= 0.f || !GetOwner()->HasAuthority())
	{
		return;
	}

	ApplyHealthEffect(UFPSRLDamageEffect::StaticClass(), FPSRLGameplayTags::SetByCaller_Damage, Damage, InstigatedBy, DamageCauser);

	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s took %.0f damage. HP now %.0f"),
		*GetOwner()->GetActorNameOrLabel(), Damage, GetCurrentHealth()), FColor::Cyan);
}

void UFPSRLHealthComponent::Heal(float Amount)
{
	if (Amount <= 0.f || !GetOwner()->HasAuthority())
	{
		return;
	}

	ApplyHealthEffect(UFPSRLHealEffect::StaticClass(), FPSRLGameplayTags::SetByCaller_Healing, Amount, nullptr, nullptr);
}

void UFPSRLHealthComponent::ApplyHealthEffect(TSubclassOf<UGameplayEffect> EffectClass, const FGameplayTag& MagnitudeTag, float Magnitude,
	AController* InstigatedBy, AActor* Causer)
{
	if (!AbilitySystemComponent)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("%s: health effect ignored, no Ability System Component initialized"), *GetNameSafe(GetOwner()));
		return;
	}

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	if (InstigatedBy || Causer)
	{
		// Record who did it, so OnDeath can report the killer (kill credit, Event.Kill for relics later).
		Context.AddInstigator(InstigatedBy, Causer);
	}

	const FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(EffectClass, 1.f, Context);
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(MagnitudeTag, Magnitude);
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

void UFPSRLHealthComponent::HandleHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	OnHealthChanged.Broadcast(GetCurrentHealth(), GetMaxHealth());
}

void UFPSRLHealthComponent::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser)
{
	AController* InstigatorController = Cast<AController>(DamageInstigator);
	if (!InstigatorController)
	{
		if (const APawn* InstigatorPawn = Cast<APawn>(DamageInstigator))
		{
			InstigatorController = InstigatorPawn->GetController();
		}
	}

	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s DIED (OnDeath broadcasting)"), *GetOwner()->GetActorNameOrLabel()), FColor::Red);
	OnDeath.Broadcast(InstigatorController, DamageCauser);
}

bool UFPSRLHealthComponent::IsDead() const
{
	// Uninitialized (e.g. a client before the set replicates) counts as alive, never as dead.
	return GetHealthSet() && GetCurrentHealth() <= 0.f;
}

int32 UFPSRLHealthComponent::CountAliveActors(const TArray<AActor*>& Actors)
{
	int32 Alive = 0;
	for (const AActor* Actor : Actors)
	{
		const UFPSRLHealthComponent* Health = Actor ? Actor->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (Health && !Health->IsDead())
		{
			++Alive;
		}
	}
	return Alive;
}

float UFPSRLHealthComponent::GetHealthPercent() const
{
	const float Max = GetMaxHealth();
	return Max > 0.f ? GetCurrentHealth() / Max : 0.f;
}

float UFPSRLHealthComponent::GetCurrentHealth() const
{
	const UFPSRLHealthSet* HealthSet = GetHealthSet();
	return HealthSet ? HealthSet->GetHealth() : 0.f;
}

float UFPSRLHealthComponent::GetMaxHealth() const
{
	const UFPSRLHealthSet* HealthSet = GetHealthSet();
	return HealthSet ? HealthSet->GetMaxHealth() : 0.f;
}
