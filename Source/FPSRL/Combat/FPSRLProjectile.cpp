// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLProjectile.h"
#include "Combat/FPSRLCombatRules.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "FPSRL.h"

AFPSRLProjectile::AFPSRLProjectile()
{
	bReplicates = true;
	SetReplicatingMovement(true);
}

static const FName DamagePropertyName(TEXT("Damage"));	// the Blueprint projectile's damage (the weapon sets it per shot)

void AFPSRLProjectile::SetProjectileDamage(AActor* Projectile, float Damage)
{
	if (FProperty* Property = Projectile ? Projectile->GetClass()->FindPropertyByName(DamagePropertyName) : nullptr)
	{
		if (FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
		{
			AsDouble->SetPropertyValue_InContainer(Projectile, Damage);
		}
		else if (FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
		{
			AsFloat->SetPropertyValue_InContainer(Projectile, Damage);
		}
	}
}

bool AFPSRLProjectile::CanPawnShoot(const APawn* Pawn)
{
	return UFPSRLHealthComponent::IsPawnUp(Pawn);
}

void AFPSRLProjectile::BeginPlay()
{
	Super::BeginPlay();

	APawn* Shooter = GetInstigator();
	UWorld* World = GetWorld();
	if (!World || !Shooter)
	{
		return;
	}

	// Projectiles never block each other (the Projectile channel blocks by default): a rifle's next bullet would hit the
	// one ahead of it (or one that bounced), and a shotgun's pellets start in the same spot. Every machine, every copy.
	// Whatever its components do later (the Blueprint turns a bullet into a physics sphere after a hit, resetting its
	// collision; the next rifle bullets piled up on those in front of the enemy), projectiles ignore each other as actors.
	if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		Body->SetCollisionResponseToChannel(Body->GetCollisionObjectType(), ECR_Ignore);
		for (TActorIterator<AFPSRLProjectile> It(World); It; ++It)
		{
			if (*It == this)
			{
				continue;
			}
			Body->IgnoreActorWhenMoving(*It, true);
			if (UPrimitiveComponent* OtherBody = Cast<UPrimitiveComponent>(It->GetRootComponent()))
			{
				OtherBody->IgnoreActorWhenMoving(this, true);
			}
		}
	}

	if (World->GetNetMode() == NM_Client)
	{
		// Authority on a client = spawned locally by this client's own weapon (replicated ones arrive as simulated).
		if (GetLocalRole() != ROLE_Authority || !Shooter->IsLocallyControlled())
		{
			return;
		}
		if (!CanPawnShoot(Shooter))
		{
			Destroy();
			return;
		}
		if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Shooter->GetController()))
		{
			PC->ServerFireProjectile(GetClass(), GetActorLocation(), GetActorRotation(), ExportSpawnSettings());
		}
		return;
	}

	// Server: a downed or dead player (e.g. the listen host) fires nothing.
	if (!CanPawnShoot(Shooter))
	{
		Destroy();
		return;
	}
	FPSRLCombat::NotifyAttack(Shooter, EFPSRLItemSource::Ranged, AttackId);	// once per shot (a player's; enemies are ignored)
}

bool AFPSRLProjectile::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (bHiddenFromInstigator)
	{
		const APawn* Shooter = GetInstigator();
		if (Shooter && (RealViewer == Shooter->GetController() || ViewTarget == Shooter))
		{
			return false;	// the shooter already sees their own local copy
		}
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

bool AFPSRLProjectile::IsMachineLocalReference(const FProperty* Property) const
{
	// Instigator is Expose on Spawn too. Sent as text it named the client's copy of the shooter, which on the server
	// resolved to nothing (or another actor): the server's projectile then hit its own shooter, was replicated back to
	// them (the "dropping" duplicate), and its damage counted as environment damage (friendly fire, cannon splash).
	// The server sets the instigator itself; asset references (classes, soft paths) are the same on every machine.
	if (Property->GetFName() == TEXT("Instigator") || Property->GetFName() == TEXT("Owner"))
	{
		return true;
	}
	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
	if (!ObjectProperty || ObjectProperty->IsA<FClassProperty>() || ObjectProperty->IsA<FSoftObjectProperty>())
	{
		return false;
	}
	const UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(this);
	return Value && (Value->IsA<AActor>() || Value->GetTypedOuter<AActor>() || Value->GetTypedOuter<ULevel>());
}

TArray<FString> AFPSRLProjectile::ExportSpawnSettings() const
{
	TArray<FString> Settings;
	for (TFieldIterator<FProperty> It(GetClass()); It; ++It)
	{
		if ((It->HasAnyPropertyFlags(CPF_ExposeOnSpawn) || It->GetFName() == DamagePropertyName) && !IsMachineLocalReference(*It))
		{
			FString Value;
			It->ExportText_InContainer(0, Value, this, nullptr, nullptr, PPF_None);
			Settings.Add(It->GetName() + TEXT("=") + Value);
		}
	}
	return Settings;
}

void AFPSRLProjectile::ImportSpawnSettings(const TArray<FString>& Settings)
{
	for (const FString& Setting : Settings)
	{
		FString Name, Value;
		if (!Setting.Split(TEXT("="), &Name, &Value))
		{
			continue;
		}
		FProperty* Property = GetClass()->FindPropertyByName(FName(*Name));
		if (!Property || (!Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn) && Property->GetFName() != DamagePropertyName) || IsMachineLocalReference(Property))
		{
			continue;	// keeps the server's own Instigator / Owner
		}
		Property->ImportText_InContainer(*Value, this, this, PPF_None);
		if (IsMachineLocalReference(Property))
		{
			Property->ClearValue_InContainer(this);	// a client can't point the projectile at a world object
		}
	}
}

static TAutoConsoleVariable<bool> CVarProjectileHits(TEXT("fpsrl.Debug.ProjectileHits"), false,
	TEXT("Log what every projectile hits (shots that don't land)."));

void AFPSRLProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (CVarProjectileHits.GetValueOnGameThread())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[ProjectileHit] %s (from %s) hit %s.%s at %s"), *GetName(), *GetNameSafe(GetInstigator()), *GetNameSafe(Other),
			*GetNameSafe(OtherComp), *HitLocation.ToCompactString());
	}
}
