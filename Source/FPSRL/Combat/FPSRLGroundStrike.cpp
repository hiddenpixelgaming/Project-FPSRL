// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLGroundStrike.h"
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

namespace FPSRLGroundStrike
{
	static const TCHAR* DiscMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* TelegraphMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyTelegraph.M_EnemyTelegraph");
	static const TCHAR* SolidMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyAimLine.M_EnemyAimLine");
	static constexpr float FlashSeconds = 0.3f;
	static constexpr float VerticalReach = 220.f;	// players this far above / below its floor are out of it (platforms)
}

AFPSRLGroundStrike::AFPSRLGroundStrike()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AFPSRLGroundStrike* AFPSRLGroundStrike::Spawn(APawn* Source, const FVector& GroundLocation, float InRadius, float InDelay, float InDamage)
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
	AFPSRLGroundStrike* Strike = World->SpawnActor<AFPSRLGroundStrike>(AFPSRLGroundStrike::StaticClass(), FTransform(GroundLocation), Params);
	if (Strike)
	{
		Strike->Radius = InRadius;
		Strike->Delay = FMath::Max(0.f, InDelay);
		Strike->Damage = InDamage;
		Strike->FinishSpawning(FTransform(GroundLocation));
	}
	return Strike;
}

void AFPSRLGroundStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFPSRLGroundStrike, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLGroundStrike, Delay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLGroundStrike, Damage, COND_InitialOnly);
}

UStaticMeshComponent* AFPSRLGroundStrike::MakeDisc(const TCHAR* Name, const TCHAR* MaterialPath)
{
	UStaticMeshComponent* Disc = NewObject<UStaticMeshComponent>(this, Name);
	Disc->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, FPSRLGroundStrike::DiscMesh));
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
	{
		Disc->SetMaterial(0, Material);
	}
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCastShadow(false);
	Disc->SetupAttachment(RootComponent);
	Disc->RegisterComponent();
	return Disc;
}

void AFPSRLGroundStrike::BeginPlay()
{
	Super::BeginPlay();
	StartTime = GetWorld()->GetTimeSeconds();
	SetLifeSpan(Delay + FPSRLGroundStrike::FlashSeconds + 0.2f);
	if (GetNetMode() != NM_DedicatedServer)
	{
		// The blast area (see-through) and a solid core that grows to fill it as the moment comes.
		const float Diameter = Radius * 2.f / 100.f;	// the engine cylinder: 100 cm across, 100 cm tall
		Area = MakeDisc(TEXT("Area"), FPSRLGroundStrike::TelegraphMaterial);
		Area->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
		Area->SetRelativeScale3D(FVector(Diameter, Diameter, 0.02f));
		Fill = MakeDisc(TEXT("Fill"), FPSRLGroundStrike::SolidMaterial);
		Fill->SetRelativeLocation(FVector(0.f, 0.f, 4.f));
		Fill->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.02f));
		Area->SetVisibility(Delay > 0.f);
		Fill->SetVisibility(Delay > 0.f);
	}
	if (Delay <= 0.f)
	{
		Impact();
	}
}

void AFPSRLGroundStrike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Age = static_cast<float>(GetWorld()->GetTimeSeconds() - StartTime);
	if (!bImpacted)
	{
		if (Fill)
		{
			const float Diameter = Radius * 2.f / 100.f * FMath::Clamp(Age / FMath::Max(0.01f, Delay), 0.f, 1.f);
			Fill->SetRelativeScale3D(FVector(FMath::Max(0.01f, Diameter), FMath::Max(0.01f, Diameter), 0.02f));
		}
		if (Age >= Delay)
		{
			Impact();
		}
		return;
	}
	if (Flash)
	{
		// The blast: a red dome that swells and is gone.
		const float Alpha = FMath::Clamp((Age - Delay) / FPSRLGroundStrike::FlashSeconds, 0.f, 1.f);
		const float Size = Radius * 2.f / 100.f * (0.4f + 0.6f * Alpha);
		Flash->SetRelativeScale3D(FVector(Size, Size, Size * 0.5f * (1.f - Alpha) + 0.01f));
		Flash->SetVisibility(Alpha < 1.f);
	}
}

void AFPSRLGroundStrike::Impact()
{
	if (bImpacted)
	{
		return;
	}
	bImpacted = true;
	if (Area)
	{
		Area->SetVisibility(false);
		Fill->SetVisibility(false);
	}
	if (GetNetMode() != NM_DedicatedServer && Damage != 0.f)
	{
		Flash = NewObject<UStaticMeshComponent>(this, TEXT("Flash"));
		Flash->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, FPSRLGroundStrike::SphereMesh));
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, FPSRLGroundStrike::SolidMaterial))
		{
			Flash->SetMaterial(0, Material);
		}
		Flash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flash->SetCastShadow(false);
		Flash->SetupAttachment(RootComponent);
		Flash->RegisterComponent();
	}
	// A server sees a marker with no damage (a warning) only as a marker; damage goes to players in the radius.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!HasAuthority() || Damage <= 0.f || !GameState)
	{
		return;
	}
	APawn* Source = GetInstigator();
	const FVector Center = GetActorLocation();
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		ACharacter* Player = PlayerState ? Cast<ACharacter>(PlayerState->GetPawn()) : nullptr;
		const UFPSRLHealthComponent* Health = Player ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (!Health || Health->IsDead())
		{
			continue;
		}
		const float Feet = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Center.Z;
		const float Distance = FVector::Dist2D(Player->GetActorLocation(), Center) - Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (Distance > Radius || FMath::Abs(Feet) > FPSRLGroundStrike::VerticalReach)
		{
			continue;
		}
		UGameplayStatics::ApplyDamage(Player, Damage, Source ? Source->GetController() : nullptr, Source ? static_cast<AActor*>(Source) : this, nullptr);
		++PlayersHit;
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Strike] %s at %s: %.0f damage in %.0f cm, %d player(s) hit"), *GetNameSafe(Source), *Center.ToCompactString(),
		Damage, Radius, PlayersHit);
}
