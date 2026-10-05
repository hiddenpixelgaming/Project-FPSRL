// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "FPSRL.h"

namespace FPSRLShockwave
{
	static constexpr int32 SegmentCount = 40;
	static constexpr float StartRadius = 60.f;
	static constexpr float Thickness = 30.f;		// the ring's wall, cm
	static constexpr float BelowTolerance = 150.f;	// players on ground this far below its floor are on other ground
	static constexpr float HigherGround = 200.f;	// ... or this far above it (a platform, a ledge): it doesn't climb there
	static const TCHAR* SegmentMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* SegmentMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyAimLine.M_EnemyAimLine");
}

AFPSRLShockwave::AFPSRLShockwave()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AFPSRLShockwave* AFPSRLShockwave::Spawn(APawn* Source, const FVector& GroundLocation, float InDamage, float InMaxRadius, float InSpeed, float InHeight)
{
	UWorld* World = Source ? Source->GetWorld() : nullptr;
	if (!World || !Source->HasAuthority())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = Source;
	Params.Instigator = Source;
	Params.bDeferConstruction = true;
	AFPSRLShockwave* Wave = World->SpawnActor<AFPSRLShockwave>(AFPSRLShockwave::StaticClass(), FTransform(GroundLocation), Params);
	if (Wave)
	{
		Wave->Damage = InDamage;
		Wave->MaxRadius = InMaxRadius;
		Wave->Speed = InSpeed;
		Wave->Height = InHeight;
		Wave->FinishSpawning(FTransform(GroundLocation));
		UE_LOG(LogFPSRL, Verbose, TEXT("[Shockwave] %s at %s: %.0f damage, %.0f cm at %.0f cm/s, %.0f cm high"), *Source->GetName(),
			*GroundLocation.ToCompactString(), InDamage, InMaxRadius, InSpeed, InHeight);
	}
	return Wave;
}

void AFPSRLShockwave::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFPSRLShockwave, MaxRadius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLShockwave, Speed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLShockwave, Height, COND_InitialOnly);
}

void AFPSRLShockwave::BeginPlay()
{
	Super::BeginPlay();
	StartTime = GetWorld()->GetTimeSeconds();
	LastRadius = FPSRLShockwave::StartRadius;
	SetLifeSpan(MaxRadius / FMath::Max(1.f, Speed) + 0.3f);
	if (GetNetMode() != NM_DedicatedServer)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, FPSRLShockwave::SegmentMesh);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, FPSRLShockwave::SegmentMaterial);
		for (int32 Index = 0; Mesh && Index < FPSRLShockwave::SegmentCount; ++Index)
		{
			UStaticMeshComponent* Segment = NewObject<UStaticMeshComponent>(this);
			Segment->SetStaticMesh(Mesh);
			if (Material)
			{
				Segment->SetMaterial(0, Material);
			}
			Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Segment->SetCastShadow(false);
			Segment->SetupAttachment(RootComponent);
			Segment->RegisterComponent();
			Segments.Add(Segment);
		}
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Shockwave] ring shown, %d segments (%s)"), Segments.Num(), HasAuthority() ? TEXT("server") : TEXT("client"));
	UpdateRing();
}

float AFPSRLShockwave::GetRadius() const
{
	const double Age = GetWorld() ? GetWorld()->GetTimeSeconds() - StartTime : 0.0;
	return FMath::Min(MaxRadius, FPSRLShockwave::StartRadius + Speed * static_cast<float>(Age));
}

void AFPSRLShockwave::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Radius = GetRadius();
	if (HasAuthority())
	{
		DamagePlayers(LastRadius, Radius);
	}
	LastRadius = Radius;
	UpdateRing();
}

void AFPSRLShockwave::UpdateRing()
{
	// Wall segments around the circle: each one's length is its share of the circumference.
	const float Radius = GetRadius();
	const float Length = 2.f * UE_PI * Radius / FPSRLShockwave::SegmentCount * 1.08f;
	const bool bDone = Radius >= MaxRadius;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const float Angle = 360.f * Index / Segments.Num();
		const FVector Direction = FRotator(0.f, Angle, 0.f).Vector();
		// Along the floor: each piece sits on the ground under it (ramps, steps, a pit's rim), never up on a platform.
		FVector Point = GetActorLocation() + Direction * Radius;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 400.f), Point - FVector(0.f, 0.f, 400.f), ECC_Visibility)
			&& Hit.ImpactPoint.Z - GetActorLocation().Z < FPSRLShockwave::HigherGround)
		{
			Point.Z = Hit.ImpactPoint.Z;
		}
		Segments[Index]->SetWorldLocationAndRotation(Point + FVector(0.f, 0.f, Height * 0.5f), FRotator(0.f, Angle + 90.f, 0.f));
		Segments[Index]->SetRelativeScale3D(FVector(Length / 100.f, FPSRLShockwave::Thickness / 100.f, Height / 100.f));
		Segments[Index]->SetVisibility(!bDone);
	}
}

void AFPSRLShockwave::DamagePlayers(float FromRadius, float ToRadius)
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState)
	{
		return;
	}
	APawn* Source = GetInstigator();
	const FVector Origin = GetActorLocation();
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		ACharacter* Player = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
		const UFPSRLHealthComponent* Health = Player ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (!Health || Health->IsDead() || Struck.Contains(Player))
		{
			continue;
		}
		// The ring swept from FromRadius to ToRadius this frame: does it cross the player's capsule?
		const float CapsuleRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float Distance = FVector::Dist2D(Player->GetActorLocation(), Origin);
		if (Distance + CapsuleRadius < FromRadius - FPSRLShockwave::Thickness || Distance - CapsuleRadius > ToRadius + FPSRLShockwave::Thickness * 0.5f)
		{
			continue;
		}
		// The ground under the player: jumped over the ring (feet above it by more than its height), or standing on other
		// ground well above (a platform) or below its floor. It runs along the main floor, up and down small steps.
		const float Feet = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		float Ground = Feet;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Player->GetActorLocation(), Player->GetActorLocation() - FVector(0.f, 0.f, 1000.f), ECC_Visibility,
			FCollisionQueryParams(TEXT("ShockwaveGround"), false, Player)))
		{
			Ground = Hit.ImpactPoint.Z;
		}
		const float AboveGround = Feet - Ground;
		const float GroundAboveFloor = Ground - Origin.Z;
		if (AboveGround > Height || GroundAboveFloor > FPSRLShockwave::HigherGround || GroundAboveFloor < -FPSRLShockwave::BelowTolerance)
		{
			UE_LOG(LogFPSRL, Verbose, TEXT("[Shockwave] %s cleared it (feet %.0f cm above their ground, ground %.0f cm from its floor)"), *Player->GetName(), AboveGround, GroundAboveFloor);
			Struck.Add(Player);	// it has passed them
			continue;
		}
		Struck.Add(Player);
		UGameplayStatics::ApplyDamage(Player, Damage, Source ? Source->GetController() : nullptr, Source ? static_cast<AActor*>(Source) : this, nullptr);
		UE_LOG(LogFPSRL, Verbose, TEXT("[Shockwave] hits %s (%.0f damage)"), *Player->GetName(), Damage);
	}
}
