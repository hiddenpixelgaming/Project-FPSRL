// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLProjectile.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"

AFPSRLProjectile::AFPSRLProjectile()
{
	bReplicates = true;
	SetReplicatingMovement(true);
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
	}
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
		if (It->HasAnyPropertyFlags(CPF_ExposeOnSpawn) && !IsMachineLocalReference(*It))
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
		if (!Property || !Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn) || IsMachineLocalReference(Property))
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
