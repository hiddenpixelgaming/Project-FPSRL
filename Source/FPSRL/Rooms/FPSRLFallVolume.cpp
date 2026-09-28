// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLFallVolume.h"
#include "Components/BoxComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLGameState.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "FPSRL.h"

AFPSRLFallVolume::AFPSRLFallVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;	// the server decides; clients see the result through the pawn and its health

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetCollisionProfileName(TEXT("OverlapAll"));
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetHiddenInGame(true);
	SetRootComponent(Volume);
}

void AFPSRLFallVolume::Cover(const FBox& Box)
{
	SetActorLocation(Box.GetCenter());
	Volume->SetBoxExtent(Box.GetExtent());
	if (HasAuthority())
	{
		Volume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleOverlap);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Fall] volume covers %s (response %s)"), *Box.ToString(),
		*UEnum::GetValueAsString(UFPSRLRunSettings::Get().FallResponse));
}

void AFPSRLFallVolume::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !OtherActor || OtherActor == this)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(OtherActor);
	const APlayerState* PlayerState = Pawn ? Pawn->GetPlayerState() : nullptr;
	if (PlayerState && !PlayerState->IsABot())
	{
		CatchPlayer(Pawn);
	}
	else if (UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Fall] %s fell out of the level: killed"), *Pawn->GetName());
		Health->Kill();	// counts as a kill, so the room it belongs to can still be cleared
	}
	else if (!OtherActor->IsA<APawn>() && OtherActor->IsRootComponentMovable())
	{
		OtherActor->Destroy();	// dropped props, projectiles
	}
}

void AFPSRLFallVolume::CatchPlayer(APawn* Pawn)
{
	UFPSRLHealthComponent* Health = Pawn->FindComponentByClass<UFPSRLHealthComponent>();
	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	if (Settings.FallResponse == EFPSRLFallResponse::InstantDeath && !(Health && Health->IsGodMode()))	// god mode: always back on the floor
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Fall] %s fell out of the level: killed (playtest setting)"), *Pawn->GetName());
		if (Health)
		{
			Health->Kill();
		}
		return;
	}

	// Back on the floor of the room they fell from.
	const AFPSRLGameState* GameState = GetWorld()->GetGameState<AFPSRLGameState>();
	FTransform Spot;
	if (!GameState || !GameState->DepthLayout->FindSafeSpot(Pawn->GetActorLocation(), Spot))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Fall] %s fell but no room to return to: killed"), *Pawn->GetName());
		if (Health)
		{
			Health->Kill();
		}
		return;
	}
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	Pawn->TeleportTo(Spot.GetLocation(), Spot.Rotator());
	if (AController* Controller = Pawn->GetController())
	{
		Controller->ClientSetRotation(Spot.Rotator());
	}
	if (Health)
	{
		Health->ApplyEnvironmentDamage(Health->GetMaxHealth() * Settings.FallDamageFraction);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Fall] %s fell out of the level: back at %s, %s"), *Pawn->GetName(), *Spot.GetLocation().ToCompactString(),
		Health && Health->IsGodMode() ? TEXT("no damage (god mode)") : *FString::Printf(TEXT("-%.0f%% health"), Settings.FallDamageFraction * 100.f));
}
