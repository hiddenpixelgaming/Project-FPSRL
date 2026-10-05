// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLLandmine.h"
#include "Combat/FPSRLGroundStrike.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "FPSRL.h"

namespace FPSRLLandmine
{
	static const TCHAR* DiscMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* AreaMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyTelegraph.M_EnemyTelegraph");
	static const TCHAR* ArmedMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyAimLine.M_EnemyAimLine");
	static const TCHAR* ArmingMaterial = TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark");
	static constexpr float BodyDiameter = 60.f;
	static constexpr float BodyHeight = 14.f;
}

AFPSRLLandmine::AFPSRLLandmine()
{
	bReplicates = true;
	SetReplicateMovement(false);
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(false);	// the server turns it on when the mine arms
	RootComponent = Trigger;
}

AFPSRLLandmine* AFPSRLLandmine::Spawn(APawn* Source, const FVector& GroundLocation, float InTriggerRadius, float InExplosionRadius, float InDamage, float InArmSeconds)
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
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSRLLandmine* Mine = World->SpawnActor<AFPSRLLandmine>(AFPSRLLandmine::StaticClass(), FTransform(GroundLocation), Params);
	if (Mine)
	{
		Mine->TriggerRadius = InTriggerRadius;
		Mine->ExplosionRadius = InExplosionRadius;
		Mine->Damage = InDamage;
		Mine->ArmSeconds = InArmSeconds;
		Mine->FinishSpawning(FTransform(GroundLocation));
	}
	return Mine;
}

TArray<AFPSRLLandmine*> AFPSRLLandmine::GetMinesOf(const APawn* Source)
{
	TArray<AFPSRLLandmine*> Mines;
	for (TActorIterator<AFPSRLLandmine> It(Source ? Source->GetWorld() : nullptr); Source && It; ++It)
	{
		if (It->GetInstigator() == Source && !It->bDetonated)
		{
			Mines.Add(*It);
		}
	}
	return Mines;
}

void AFPSRLLandmine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AFPSRLLandmine, TriggerRadius, COND_InitialOnly);
	DOREPLIFETIME(AFPSRLLandmine, bArmed);
}

void AFPSRLLandmine::BeginPlay()
{
	Super::BeginPlay();
	Trigger->SetSphereRadius(TriggerRadius);
	if (GetNetMode() != NM_DedicatedServer)
	{
		// Its trigger area on the floor (see-through red, shown from the moment it lands) and the mine itself.
		auto MakeDisc = [this](const TCHAR* Name, const TCHAR* MaterialPath, float Diameter, float Height, float Z)
		{
			UStaticMeshComponent* Disc = NewObject<UStaticMeshComponent>(this, Name);
			Disc->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, FPSRLLandmine::DiscMesh));
			if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
			{
				Disc->SetMaterial(0, Material);
			}
			Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Disc->SetCastShadow(false);
			Disc->SetupAttachment(RootComponent);
			Disc->SetRelativeLocation(FVector(0.f, 0.f, Z));
			Disc->SetRelativeScale3D(FVector(Diameter / 100.f, Diameter / 100.f, Height / 100.f));
			Disc->RegisterComponent();
			return Disc;
		};
		Area = MakeDisc(TEXT("Area"), FPSRLLandmine::AreaMaterial, TriggerRadius * 2.f, 2.f, 2.f);
		Body = MakeDisc(TEXT("Body"), FPSRLLandmine::ArmingMaterial, FPSRLLandmine::BodyDiameter, FPSRLLandmine::BodyHeight, FPSRLLandmine::BodyHeight * 0.5f);
		OnRep_Armed();
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Mine] placed at %s (%s)"), *GetActorLocation().ToCompactString(), HasAuthority() ? TEXT("server") : TEXT("client"));
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);
		GetWorldTimerManager().SetTimer(ArmTimer, this, &ThisClass::Arm, FMath::Max(0.05f, ArmSeconds), false);
	}
}

void AFPSRLLandmine::Arm()
{
	bArmed = true;
	OnRep_Armed();
	ForceNetUpdate();
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->UpdateOverlaps();
	// Someone already standing in it when it arms sets it off.
	TArray<AActor*> Inside;
	Trigger->GetOverlappingActors(Inside, APawn::StaticClass());
	for (const AActor* Actor : Inside)
	{
		if (IsTriggeringPlayer(Actor))
		{
			Detonate();
			return;
		}
	}
}

void AFPSRLLandmine::OnRep_Armed()
{
	if (Body)
	{
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, bArmed ? FPSRLLandmine::ArmedMaterial : FPSRLLandmine::ArmingMaterial))
		{
			Body->SetMaterial(0, Material);
		}
	}
}

bool AFPSRLLandmine::IsTriggeringPlayer(const AActor* Actor) const
{
	const APawn* Pawn = Cast<APawn>(Actor);
	const UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	return Health && !Health->IsDead() && UFPSRLHealthComponent::IsPlayerSide(nullptr, Pawn);
}

void AFPSRLLandmine::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bArmed && IsTriggeringPlayer(OtherActor))
	{
		Detonate();
	}
}

void AFPSRLLandmine::Detonate()
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}
	bDetonated = true;
	AFPSRLGroundStrike::Spawn(GetInstigator(), GetActorLocation(), ExplosionRadius, 0.f, Damage);
	UE_LOG(LogFPSRL, Log, TEXT("[Mine] %s's mine at %s went off"), *GetNameSafe(GetInstigator()), *GetActorLocation().ToCompactString());
	Destroy();
}
