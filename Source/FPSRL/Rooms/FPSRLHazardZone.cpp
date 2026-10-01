// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLHazardZone.h"
#include "Components/BoxComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "GameFramework/Pawn.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "TimerManager.h"
#include "FPSRL.h"

AFPSRLHazardZone::AFPSRLHazardZone()
{
	PrimaryActorTick.bCanEverTick = false;
	Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
	Zone->SetBoxExtent(FVector(500.f, 500.f, 50.f));
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionResponseToAllChannels(ECR_Ignore);
	Zone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Zone->SetGenerateOverlapEvents(true);
	Zone->SetCanEverAffectNavigation(true);
	RootComponent = Zone;

	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Null::StaticClass());
}

void AFPSRLHazardZone::BeginPlay()
{
	Super::BeginPlay();
	if (!bBlocksEnemyNavigation)
	{
		NavModifier->SetAreaClass(nullptr);
	}
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(DamageTimer, this, &ThisClass::ApplyDamage, DamageInterval, true);
	}
}

void AFPSRLHazardZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DamageTimer);
	Super::EndPlay(EndPlayReason);
}

void AFPSRLHazardZone::ApplyDamage()
{
	TArray<AActor*> Inside;
	Zone->GetOverlappingActors(Inside, APawn::StaticClass());
	for (AActor* Actor : Inside)
	{
		UFPSRLHealthComponent* Health = Actor->FindComponentByClass<UFPSRLHealthComponent>();
		if (!Health || !UFPSRLHealthComponent::IsPawnUp(Cast<APawn>(Actor)))
		{
			continue;	// dead or downed: the hazard doesn't finish anyone off
		}
		const bool bPlayer = UFPSRLHealthComponent::IsPlayerSide(nullptr, Actor);
		if ((bPlayer && !bAffectsPlayers) || (!bPlayer && !bAffectsEnemies))
		{
			continue;
		}
		Health->ApplyEnvironmentDamage(DamagePerSecond * DamageInterval);
		++TicksApplied;
	}
}
