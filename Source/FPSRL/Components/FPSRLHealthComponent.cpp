// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/FPSRLHealthComponent.h"
#include "Components/FPSRLDownedAnimComponent.h"
#include "UI/FPSRLEnemyHealthBar.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Perception/AIPerceptionComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Abilities/Effects/FPSRLHealthEffects.h"
#include "Engine/Engine.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Types/FPSRLGameplayTags.h"
#include "Data/FPSRLRunSettings.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Rooms/FPSRLReviveMarker.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Combat/FPSRLProjectile.h"
#include "Combat/FPSRLCombatRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
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
			ApplyEnemyTestTint();
			UFPSRLEnemyHealthBarComponent::AddTo(Cast<APawn>(GetOwner()), this, OwnerASC);	// overhead bar (ordinary enemies)
		}
	}
}

namespace FPSRLHealthDebug
{
	static TAutoConsoleVariable<bool> CVarTintEnemies(TEXT("fpsrl.TintEnemies"), true,
		TEXT("Testing: paint enemies solid yellow so they stand out from players (applies to enemies spawned afterwards)."));
}

void UFPSRLHealthComponent::SetKeepsOwnColours()
{
	bKeepsOwnColours = true;
	if (bRestingTintIsEnemyTint)
	{
		bRestingTintIsEnemyTint = false;
		RestoreBodyColor();	// the tint was already on: back to the original colours
	}
}

void UFPSRLHealthComponent::ApplyEnemyTestTint()
{
	if (!FPSRLHealthDebug::CVarTintEnemies.GetValueOnGameThread() || GetNetMode() == NM_DedicatedServer || bKeepsOwnColours)
	{
		return;
	}
	// Tint the mesh's own materials. A swapped-in material must be flagged "Used with Skeletal Mesh" or a packaged build
	// renders the default grey instead (v0.1.2: the engine BasicShapeMaterial tinted in the editor, grey in the build).
	// The mannequin material has no single body-colour switch, so every colour/tint vector parameter it exposes is set.
	bRestingTintIsEnemyTint = true;
	SetBodyColor(EnemyTestTintColor);
}

void UFPSRLHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitFlashTimer);
	}
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

		bDied = false;

		// Fresh spawn: full health. (A player's set outlives the pawn, so this also resets it on respawn.)
		InASC->SetNumericAttributeBase(UFPSRLHealthSet::GetMaxHealthAttribute(), DefaultMaxHealth);
		InASC->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), DefaultMaxHealth);

		HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
		HealthSet->OnDowned.AddUObject(this, &ThisClass::HandleDowned);
	}

	// Downed state arrives as a replicated tag, so the crawl applies on server and clients alike.
	DownedTagHandle = InASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Downed, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleDownedTagChanged);

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
		HealthSet->OnDowned.RemoveAll(this);
	}
	AbilitySystemComponent->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Downed, EGameplayTagEventType::NewOrRemoved).Remove(DownedTagHandle);
	if (ReviveMarker)
	{
		ReviveMarker->Destroy();
		ReviveMarker = nullptr;
	}

	AbilitySystemComponent = nullptr;
}

const UFPSRLHealthSet* UFPSRLHealthComponent::GetHealthSet() const
{
	return AbilitySystemComponent ? AbilitySystemComponent->GetSet<UFPSRLHealthSet>() : nullptr;
}

void UFPSRLHealthComponent::HandleTakeAnyDamage(float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (Damage <= 0.f || !GetOwner()->HasAuthority() || bDied)
	{
		return;	// nothing to do, or a corpse being shot
	}
	if (IsGodMode())
	{
		return;
	}
	if (!ShouldAcceptDamageFrom(InstigatedBy, DamageCauser))
	{
		return;
	}
	const bool bProjectile = Cast<AFPSRLProjectile>(DamageCauser) != nullptr;
	if (bPhased && bProjectile)
	{
		return;	// phased: shots do nothing (melee still lands)
	}

	// An enemy's attack: scaled by its encounter scaling (player count, Depth, difficulty...).
	const APawn* Attacker = InstigatedBy ? InstigatedBy->GetPawn() : nullptr;
	Attacker = Attacker ? Attacker : Cast<APawn>(DamageCauser);
	Attacker = Attacker ? Attacker : (DamageCauser ? DamageCauser->GetInstigator() : nullptr);
	if (Attacker && !IsPlayerSide(nullptr, Attacker))
	{
		if (const UFPSRLHealthComponent* AttackerHealth = Attacker->FindComponentByClass<UFPSRLHealthComponent>())
		{
			Damage *= AttackerHealth->OutgoingDamageMultiplier;
		}
	}

	// A player's attack: scaled by their combat stats for that source, maybe a critical, and reported to their Blessings.
	FPSRLCombat::FPlayerHit PlayerHit = FPSRLCombat::ResolvePlayerHit(InstigatedBy, DamageCauser, GetOwner(), Damage);

	ApplyHealthEffect(UFPSRLDamageEffect::StaticClass(), FPSRLGameplayTags::SetByCaller_Damage, Damage, InstigatedBy, DamageCauser);
	OnDamagedBy.Broadcast(Damage, const_cast<APawn*>(Attacker));
	if (bProjectile)
	{
		OnProjectileHit.Broadcast(Damage, const_cast<APawn*>(Attacker));
	}

	if (PlayerHit.AttackerASC)
	{
		FPSRLCombat::SendHitEvents(PlayerHit, GetOwner(), Damage, IsDead() || GetCurrentHealth() <= 0.f);

		// The attacker sees and hears it land (hit marker, sound; melee also a view punch).
		if (AFPSRLPlayerController* AttackerPC = PlayerHit.AttackerState ? Cast<AFPSRLPlayerController>(PlayerHit.AttackerState->GetPlayerController()) : nullptr)
		{
			const bool bKilled = IsDead() || GetCurrentHealth() <= 0.f;
			const bool bMelee = PlayerHit.Source == EFPSRLItemSource::Melee;
			const EFPSRLHitFeedback Kind = bMelee ? (bKilled ? EFPSRLHitFeedback::MeleeKill : EFPSRLHitFeedback::MeleeHit)
				: bKilled ? EFPSRLHitFeedback::Kill : PlayerHit.bCritical ? EFPSRLHitFeedback::Critical : EFPSRLHitFeedback::Hit;
			// The damage number floats up from just above the target's head (capsule top, or the actor's bounds).
			FVector NumberAt = GetOwner()->GetActorLocation();
			if (const ACharacter* Victim = Cast<ACharacter>(GetOwner()); Victim && Victim->GetCapsuleComponent())
			{
				NumberAt.Z += Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 25.f;
			}
			else
			{
				FVector Origin, Extent;
				GetOwner()->GetActorBounds(true, Origin, Extent);
				NumberAt.Z = Origin.Z + Extent.Z + 25.f;
			}
			AttackerPC->ClientCombatFeedback(Kind, Damage, PlayerHit.bCritical, NumberAt);
		}
	}

	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s took %.0f damage. HP now %.0f"),
		*GetOwner()->GetActorNameOrLabel(), Damage, GetCurrentHealth()), FColor::Cyan);
}

bool UFPSRLHealthComponent::IsPlayerSide(const AController* Controller, const AActor* Actor)
{
	if (Controller)
	{
		return Controller->IsPlayerController();
	}
	const APawn* Pawn = Cast<APawn>(Actor);
	const APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr;
	return PlayerState && !PlayerState->IsABot();
}

bool UFPSRLHealthComponent::ShouldAcceptDamageFrom(const AController* InstigatedBy, const AActor* DamageCauser) const
{
	// Who is attacking: the instigating controller's pawn, else the causer (a pawn, or a projectile / explosion whose
	// instigator is the pawn that fired it).
	const APawn* Attacker = InstigatedBy ? InstigatedBy->GetPawn() : nullptr;
	if (!Attacker && DamageCauser)
	{
		Attacker = Cast<APawn>(DamageCauser);
		if (!Attacker)
		{
			Attacker = DamageCauser->GetInstigator();
		}
	}
	const AController* AttackerController = InstigatedBy ? InstigatedBy : (Attacker ? Attacker->GetController() : nullptr);
	if (!AttackerController && !Attacker)
	{
		// A projectile / explosion that lost its shooter never hurts players (splash from a player's cannon included).
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (Cast<AFPSRLProjectile>(DamageCauser) && IsPlayerSide(OwnerPawn ? OwnerPawn->GetController() : nullptr, GetOwner()))
		{
			return false;
		}
		return true;	// environment (fall damage, hazards): no side to compare
	}

	// Downed players deal no damage at all.
	if (Attacker)
	{
		const UFPSRLHealthComponent* AttackerHealth = Attacker->FindComponentByClass<UFPSRLHealthComponent>();
		if (AttackerHealth && AttackerHealth->IsDowned())
		{
			return false;
		}
	}

	// No friendly fire: players never hurt players (themselves included), enemies never hurt enemies.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return IsPlayerSide(AttackerController, Attacker) != IsPlayerSide(OwnerPawn ? OwnerPawn->GetController() : nullptr, GetOwner());
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

	// Enemies flash red / white when hurt (players get a screen flash from their HUD instead).
	const float Health = GetCurrentHealth();
	if (LastHealthSeen >= 0.f && Health < LastHealthSeen - 0.01f && !IsPlayerSide(nullptr, GetOwner()))
	{
		StartHitFlash();
	}
	LastHealthSeen = Health;

	// After the broadcast, so the character's own death handling (ragdoll profile) has already run.
	UpdateProjectileBlocking();
}

void UFPSRLHealthComponent::UpdateProjectileBlocking()
{
	// Dead and DOWNED bodies let bullets through (every machine: health and the downed tag replicate, and clients fly
	// their own projectile copies). Downed (held at 1 health) used to soak up the shots aimed at whoever revived them,
	// so enemies could never interrupt a revive (playtest v0.1.34). A revived player blocks again.
	const bool bIgnore = (GetHealthSet() && GetCurrentHealth() <= 0.f) || IsDowned() || bDied || bPhased;
	if (bIgnore == bBodyIgnoresProjectiles)
	{
		return;
	}
	bBodyIgnoresProjectiles = bIgnore;
	if (bIgnore)
	{
		SavedProjectileResponses.Reset();
		TInlineComponentArray<UPrimitiveComponent*> Primitives(GetOwner());
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			SavedProjectileResponses.Emplace(Primitive, Primitive->GetCollisionResponseToChannel(ProjectileChannel));
			Primitive->SetCollisionResponseToChannel(ProjectileChannel, ECR_Ignore);
		}
	}
	else
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, ECollisionResponse>& Saved : SavedProjectileResponses)
		{
			if (UPrimitiveComponent* Primitive = Saved.Key.Get())
			{
				Primitive->SetCollisionResponseToChannel(ProjectileChannel, Saved.Value);
			}
		}
		SavedProjectileResponses.Reset();
	}
}

void UFPSRLHealthComponent::HandleOutOfHealth(AActor* DamageInstigator, AActor* DamageCauser)
{
	if (bDied)
	{
		return;	// already dead (e.g. finished off, then hit again at 0)
	}
	AController* InstigatorController = Cast<AController>(DamageInstigator);
	if (!InstigatorController)
	{
		if (const APawn* InstigatorPawn = Cast<APawn>(DamageInstigator))
		{
			InstigatorController = InstigatorPawn->GetController();
		}
	}

	bDied = true;
	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s DIED (OnDeath broadcasting)"), *GetOwner()->GetActorNameOrLabel()), FColor::Red);
	OnDeath.Broadcast(InstigatorController, DamageCauser);
}

bool UFPSRLHealthComponent::IsDead() const
{
	// Died stays true after the body is removed (EndPlay detaches the ASC). Uninitialized (e.g. a client before the
	// set replicates) otherwise counts as alive, never as dead.
	return bDied || (GetHealthSet() && GetCurrentHealth() <= 0.f);
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

// --- Downed ----------------------------------------------------------------------------------------------------------

bool UFPSRLHealthComponent::IsDowned() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downed);
}

bool UFPSRLHealthComponent::IsPawnUp(const APawn* Pawn)
{
	const UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	return IsValid(Pawn) && !(Health && (Health->IsDead() || Health->IsDowned()));
}

bool UFPSRLHealthComponent::IsAnyOtherPlayerUp() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState)
	{
		return false;
	}
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		const APawn* Pawn = (Player && !Player->IsInactive()) ? Player->GetPawn() : nullptr;
		if (Pawn && Pawn != OwnerPawn && IsPawnUp(Pawn))
		{
			return true;
		}
	}
	return false;
}

void UFPSRLHealthComponent::HandleDowned(AActor* DamageInstigator, AActor* DamageCauser)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!AbilitySystemComponent || !OwnerPawn || !GetOwner()->HasAuthority() || IsDowned() || bDied)
	{
		return;
	}

	if (!IsAnyOtherPlayerUp())
	{
		// Nobody left to revive anyone: this player dies, and so does everyone already down (party wipe).
		UE_LOG(LogFPSRL, Log, TEXT("%s went down with nobody left standing"), *OwnerPawn->GetActorNameOrLabel());
		if (const AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			for (const APlayerState* Player : GameState->PlayerArray)
			{
				UFPSRLHealthComponent* Other = (Player && Player->GetPawn()) ? Player->GetPawn()->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
				if (Other && Other != this && Other->IsDowned())
				{
					Other->Kill();
				}
			}
		}
		Kill();
		return;
	}

	// Down: can't be hurt further, crawls, and carries a "Press E to Revive" station for teammates.
	AbilitySystemComponent->AddLooseGameplayTag(FPSRLGameplayTags::Status_Downed, 1, EGameplayTagReplicationState::TagOnly);
	AbilitySystemComponent->AddLooseGameplayTag(FPSRLGameplayTags::Status_Invulnerable, 1, EGameplayTagReplicationState::TagOnly);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSRLReviveMarker* Marker = GetWorld()->SpawnActor<AFPSRLReviveMarker>(OwnerPawn->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (Marker)
	{
		Marker->SetTarget(OwnerPawn);
	}
	ReviveMarker = Marker;

	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s is DOWN"), *OwnerPawn->GetActorNameOrLabel()), FColor::Orange);
}

void UFPSRLHealthComponent::Revive(float HealthFraction)
{
	if (!AbilitySystemComponent || !GetOwner()->HasAuthority() || !IsDowned() || bDied)
	{
		return;
	}

	AbilitySystemComponent->SetLooseGameplayTagCount(FPSRLGameplayTags::Status_Downed, 0, EGameplayTagReplicationState::TagOnly);
	AbilitySystemComponent->SetLooseGameplayTagCount(FPSRLGameplayTags::Status_Invulnerable, 0, EGameplayTagReplicationState::TagOnly);
	if (ReviveMarker)
	{
		ReviveMarker->Destroy();
		ReviveMarker = nullptr;
	}

	const float NewHealth = FMath::Max(1.f, GetMaxHealth() * HealthFraction);
	AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), NewHealth);
	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s was revived (%.0f HP)"), *GetOwner()->GetActorNameOrLabel(), NewHealth), FColor::Green);
}

void UFPSRLHealthComponent::ScaleMaxHealth(float Multiplier)
{
	if (!GetOwner()->HasAuthority() || Multiplier <= 0.f || Multiplier == 1.f)
	{
		return;
	}
	DefaultMaxHealth *= Multiplier;
	if (AbilitySystemComponent && !bDied)
	{
		AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetMaxHealthAttribute(), DefaultMaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), DefaultMaxHealth);
	}
}

void UFPSRLHealthComponent::SetMaxHealthServer(float NewMax)
{
	if (!GetOwner()->HasAuthority() || NewMax <= 0.f)
	{
		return;
	}
	if (AbilitySystemComponent && !bDied)
	{
		AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetMaxHealthAttribute(), NewMax);
		AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), NewMax);
	}
	else
	{
		DefaultMaxHealth = NewMax;	// not initialized yet: it starts at this
	}
}

void UFPSRLHealthComponent::ApplyEnvironmentDamage(float Amount)
{
	if (!AbilitySystemComponent || !GetOwner()->HasAuthority() || bDied || Amount <= 0.f || IsGodMode())
	{
		return;
	}
	ApplyHealthEffect(UFPSRLDamageEffect::StaticClass(), FPSRLGameplayTags::SetByCaller_Damage, Amount, nullptr, nullptr);
	OnDamagedBy.Broadcast(Amount, nullptr);	// no attacker (hazards also count toward interrupting a revive)
	FPSRLHealthDebug::Show(FString::Printf(TEXT("%s took %.0f environment damage. HP now %.0f"),
		*GetOwner()->GetActorNameOrLabel(), Amount, GetCurrentHealth()), FColor::Orange);
}

void UFPSRLHealthComponent::Kill()
{
	if (!AbilitySystemComponent || !GetOwner()->HasAuthority() || bDied || IsGodMode())
	{
		return;
	}

	// OnDeath first (death screen etc.; also marks bDied so clearing Downed below doesn't make the body an AI target
	// again), then health 0 so the character's own death handling (ragdoll) runs.
	HandleOutOfHealth(nullptr, nullptr);

	AbilitySystemComponent->SetLooseGameplayTagCount(FPSRLGameplayTags::Status_Downed, 0, EGameplayTagReplicationState::TagOnly);
	AbilitySystemComponent->SetLooseGameplayTagCount(FPSRLGameplayTags::Status_Invulnerable, 0, EGameplayTagReplicationState::TagOnly);
	if (ReviveMarker)
	{
		ReviveMarker->Destroy();
		ReviveMarker = nullptr;
	}

	AbilitySystemComponent->SetNumericAttributeBase(UFPSRLHealthSet::GetHealthAttribute(), 0.f);
}

void UFPSRLHealthComponent::SetTargetableByAI(bool bTargetable)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->HasAuthority() || AITargetTag.IsNone())
	{
		return;
	}

	// The shooter AI only acquires actors carrying its sense tag ("Player").
	if (bTargetable)
	{
		if (bRemovedAITargetTag)
		{
			OwnerPawn->Tags.AddUnique(AITargetTag);
			bRemovedAITargetTag = false;
		}
	}
	else if (OwnerPawn->Tags.Remove(AITargetTag) > 0)
	{
		bRemovedAITargetTag = true;
	}

	// Every enemy drops this pawn now: forgetting clears the AI's current target (its StateTree listens to
	// OnTargetPerceptionForgotten) and resets sight, so after a revive they re-acquire as soon as they see the player.
	for (TActorIterator<AAIController> It(GetWorld()); It; ++It)
	{
		if (UAIPerceptionComponent* Perception = It->GetPerceptionComponent())
		{
			Perception->ForgetActor(OwnerPawn);
			if (!bTargetable)
			{
				Perception->OnTargetPerceptionForgotten.Broadcast(OwnerPawn);
			}
		}
	}
}

void UFPSRLHealthComponent::HandleDownedTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	const bool bDowned = NewCount > 0;
	SetTargetableByAI(!bDowned && !bDied);	// a downed player who dies stays untargetable
	UpdateProjectileBlocking();	// downed: shots pass through to whoever revives them; revived: blocks again
	ApplyDownedMovement(bDowned);
	OnDownedChanged.Broadcast(bDowned);

	// The body: falls and crawls while down, stands up when revived (players; every machine).
	if (IsPlayerSide(nullptr, GetOwner()))
	{
		UFPSRLDownedAnimComponent* DownedAnim = GetOwner()->FindComponentByClass<UFPSRLDownedAnimComponent>();
		if (!DownedAnim && bDowned)
		{
			DownedAnim = NewObject<UFPSRLDownedAnimComponent>(GetOwner(), TEXT("DownedAnimation"));
			DownedAnim->RegisterComponent();
		}
		if (DownedAnim)
		{
			DownedAnim->SetDowned(bDowned, bDied);
		}
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (AFPSRLPlayerController* PC = OwnerPawn && OwnerPawn->IsLocallyControlled() ? Cast<AFPSRLPlayerController>(OwnerPawn->GetController()) : nullptr)
	{
		PC->SetWeaponInputBlocked(bDowned);	// downed players can't shoot, aim, reload, melee or dash
	}
	if (IsPlayerSide(nullptr, GetOwner()))
	{
		// Downed players turn purple for everyone until revived.
		if (bDowned)
		{
			UE_LOG(LogFPSRL, Verbose, TEXT("[Downed] %s turns purple"), *GetOwner()->GetName());
			SetBodyColor(FLinearColor(0.55f, 0.1f, 1.f));
		}
		else
		{
			RestoreBodyColor();
		}
	}
	if (bDowned || !IsDead())	// bled out / party wipe: stay on the body until the death screen takes over
	{
		ApplyDownedCamera(bDowned);
	}
	if (bDowned && OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		FPSRLHealthDebug::Show(TEXT("You are DOWN - crawl to safety, a teammate can revive you (E)"), FColor::Orange);
	}
}

void UFPSRLHealthComponent::ApplyDownedMovement(bool bDowned)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || bDowned == bDownedMovementApplied)
	{
		return;
	}
	bDownedMovementApplied = bDowned;

	if (bDowned)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		SavedJumpZVelocity = Movement->JumpZVelocity;
		bSavedCanWalkOffLedges = Movement->bCanWalkOffLedges;
		bSavedCanWalkOffLedgesWhenCrouching = Movement->bCanWalkOffLedgesWhenCrouching;

		Movement->MaxWalkSpeed = SavedMaxWalkSpeed * UFPSRLRunSettings::Get().DownedMoveSpeedMultiplier;
		Movement->JumpZVelocity = 0.f;				// no jumping while down
		Movement->bCanWalkOffLedges = false;			// can't crawl off edges
		Movement->bCanWalkOffLedgesWhenCrouching = false;
	}
	else
	{
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
		Movement->JumpZVelocity = SavedJumpZVelocity;
		Movement->bCanWalkOffLedges = bSavedCanWalkOffLedges;
		Movement->bCanWalkOffLedgesWhenCrouching = bSavedCanWalkOffLedgesWhenCrouching;
	}
}

void UFPSRLHealthComponent::ApplyDownedCamera(bool bDowned)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || bDowned == bDownedCameraApplied || (bDowned && !OwnerPawn->IsLocallyControlled()))
	{
		return;
	}
	bDownedCameraApplied = bDowned;

	if (!bDowned)
	{
		// Revived: first person again, exactly as it was.
		for (const FDownedViewChange& Change : DownedViewChanges)
		{
			if (UPrimitiveComponent* Primitive = Change.Primitive.Get())
			{
				Primitive->SetFirstPersonPrimitiveType(Change.FirstPersonType);
				Primitive->SetOwnerNoSee(Change.bOwnerNoSee);
				Primitive->SetHiddenInGame(Change.bHiddenInGame);
			}
		}
		DownedViewChanges.Reset();
		if (DownedCamera)
		{
			DownedCamera->Deactivate();
		}
		for (const TWeakObjectPtr<UCameraComponent>& Camera : DownedDeactivatedCameras)
		{
			if (Camera.IsValid())
			{
				Camera->Activate();
			}
		}
		DownedDeactivatedCameras.Reset();
		return;
	}

	if (!DownedCamera)
	{
		DownedSpringArm = NewObject<USpringArmComponent>(OwnerPawn, TEXT("DownedSpringArm"));
		DownedSpringArm->SetupAttachment(OwnerPawn->GetRootComponent());
		DownedSpringArm->TargetArmLength = DownedCameraDistance;
		DownedSpringArm->SocketOffset = DownedCameraOffset;
		DownedSpringArm->bUsePawnControlRotation = true;	// mouse look orbits the body
		DownedSpringArm->RegisterComponent();

		DownedCamera = NewObject<UCameraComponent>(OwnerPawn, TEXT("DownedCamera"));
		DownedCamera->bAutoActivate = false;
		DownedCamera->SetupAttachment(DownedSpringArm, USpringArmComponent::SocketName);
		DownedCamera->RegisterComponent();
	}

	// The view uses the pawn's first active camera, so switch the first-person one off.
	TInlineComponentArray<UCameraComponent*> Cameras(OwnerPawn);
	for (UCameraComponent* Camera : Cameras)
	{
		if (Camera != DownedCamera && Camera->IsActive())
		{
			Camera->Deactivate();
			DownedDeactivatedCameras.Add(Camera);
		}
	}
	DownedCamera->Activate();

	// Owner-only first-person pieces (arms, first-person weapon) hidden; the world-space body and third-person weapon,
	// normally hidden from their owner, shown. Weapons are separate actors attached to the pawn.
	TArray<AActor*> Actors;
	OwnerPawn->GetAttachedActors(Actors, true, true);
	Actors.Add(OwnerPawn);
	for (AActor* Actor : Actors)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			const bool bFirstPersonOnly = Primitive->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson || Primitive->bOnlyOwnerSee;
			const bool bHiddenFromOwner = Primitive->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::WorldSpaceRepresentation || Primitive->bOwnerNoSee;
			if (!bFirstPersonOnly && !bHiddenFromOwner)
			{
				continue;
			}
			DownedViewChanges.Add({ Primitive, Primitive->FirstPersonPrimitiveType, Primitive->bOwnerNoSee != 0, Primitive->bHiddenInGame != 0 });
			if (bFirstPersonOnly)
			{
				Primitive->SetHiddenInGame(true);
			}
			else
			{
				Primitive->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
				Primitive->SetOwnerNoSee(false);
			}
		}
	}
}

bool UFPSRLHealthComponent::IsGodMode() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AFPSRLPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	return PlayerState && PlayerState->bGodMode;
#endif
}

// --- Body colour: enemy hit flash, downed players --------------------------------------------------------------------

void UFPSRLHealthComponent::SetBodyColor(const FLinearColor& Color)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(GetOwner());
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
		{
			UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(Index);
			if (!Material)
			{
				continue;
			}
			TArray<FMaterialParameterInfo> Parameters;
			TArray<FGuid> Ids;
			Material->GetAllVectorParameterInfo(Parameters, Ids);
			const bool bFirstTime = !OriginalBodyColors.Contains(Material);
			TArray<TPair<FName, FLinearColor>>& Originals = OriginalBodyColors.FindOrAdd(Material);
			for (const FMaterialParameterInfo& Parameter : Parameters)
			{
				// The mannequin material has no single body-colour switch: every colour / tint parameter is set.
				const FString Name = Parameter.Name.ToString();
				if ((Name.Contains(TEXT("Tint")) || Name.Contains(TEXT("Color"))) && !Name.Contains(TEXT("Emissive")) && !Name.Contains(TEXT("Subsurface")))
				{
					if (bFirstTime)
					{
						FLinearColor Original;
						Material->GetVectorParameterValue(Parameter, Original);
						Originals.Emplace(Parameter.Name, Original);
					}
					Material->SetVectorParameterValue(Parameter.Name, Color);
				}
			}
		}
	}
}

void UFPSRLHealthComponent::RestoreBodyColor()
{
	if (bRestingTintIsEnemyTint)
	{
		SetBodyColor(EnemyTestTintColor);
		return;
	}
	for (const TPair<TWeakObjectPtr<UMaterialInstanceDynamic>, TArray<TPair<FName, FLinearColor>>>& Entry : OriginalBodyColors)
	{
		if (UMaterialInstanceDynamic* Material = Entry.Key.Get())
		{
			for (const TPair<FName, FLinearColor>& Value : Entry.Value)
			{
				Material->SetVectorParameterValue(Value.Key, Value.Value);
			}
		}
	}
}

void UFPSRLHealthComponent::StartHitFlash()
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Flash] %s hit flash"), *GetOwner()->GetName());
	HitFlashStage = 0;
	SetBodyColor(FLinearColor(1.f, 0.05f, 0.05f));	// red
	GetWorld()->GetTimerManager().SetTimer(HitFlashTimer, this, &ThisClass::StepHitFlash, 0.05f, false);
}

void UFPSRLHealthComponent::StepHitFlash()
{
	if (HitFlashStage++ == 0)
	{
		SetBodyColor(FLinearColor::White);
		GetWorld()->GetTimerManager().SetTimer(HitFlashTimer, this, &ThisClass::StepHitFlash, 0.05f, false);
	}
	else
	{
		RestoreBodyColor();
	}
}

void UFPSRLHealthComponent::SetPhased(bool bInPhased)
{
	bPhased = bInPhased;
	UpdateProjectileBlocking();	// phased bodies let projectiles through
}
