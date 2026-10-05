// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLLandmine.h"
#include "Combat/FPSRLGroundStrike.h"
#include "Components/CapsuleComponent.h"
#include "Data/FPSRLEnemyBehaviorProfile.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Rooms/FPSRLRoom.h"
#include "Rooms/FPSRLShieldPlatform.h"
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
	PrimaryActorTick.bCanEverTick = true;	// only while it flies (a thrown mine)
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(false);
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(false);	// the server turns it on when the mine arms
	RootComponent = Trigger;
}

AFPSRLLandmine* AFPSRLLandmine::Spawn(APawn* Source, const FVector& GroundLocation, float InTriggerRadius, float InExplosionRadius, float InDamage, float InArmSeconds,
	const FVector& InFlightFrom, float InFlightSeconds)
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
		Mine->Destination = GroundLocation;
		Mine->FlightFrom = InFlightFrom;
		Mine->FlightSeconds = FMath::Max(0.f, InFlightSeconds);
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
	DOREPLIFETIME_CONDITION(AFPSRLLandmine, FlightFrom, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLLandmine, Destination, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AFPSRLLandmine, FlightSeconds, COND_InitialOnly);
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
	}
	if (FlightSeconds > 0.f)
	{
		// Thrown: out of the boss on an arc; its area shows once it lands.
		FlightStart = GetWorld()->GetTimeSeconds();
		SetActorLocation(FlightFrom);
		if (Area)
		{
			Area->SetVisibility(false);
		}
		SetActorTickEnabled(true);
	}
	else
	{
		Land();
	}
}

void AFPSRLLandmine::Arm()
{
	UE_LOG(LogFPSRL, Verbose, TEXT("[Mine] armed at %s"), *GetActorLocation().ToCompactString());
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

void AFPSRLLandmine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Alpha = FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds() - FlightStart) / FMath::Max(0.01f, FlightSeconds), 0.f, 1.f);
	if (Alpha >= 1.f)
	{
		Land();
		return;
	}
	const float Height = 250.f + FVector::Dist2D(FlightFrom, Destination) * 0.2f;
	SetActorLocation(FMath::Lerp(FlightFrom, Destination, Alpha) + FVector(0.f, 0.f, Height * 4.f * Alpha * (1.f - Alpha)));
}

void AFPSRLLandmine::Land()
{
	bLanded = true;
	SetActorTickEnabled(false);
	if (FlightSeconds > 0.f)
	{
		SetActorLocation(Destination);
	}
	if (Area)
	{
		Area->SetVisibility(true);
	}
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ArmTimer, this, &ThisClass::Arm, FMath::Max(0.05f, ArmSeconds), false);
	}
}

int32 AFPSRLLandmine::ThrowAround(APawn* Boss, const FFPSRLMineSettings& Settings)
{
	const ACharacter* Character = Cast<ACharacter>(Boss);
	UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Character || !Nav || !Boss->HasAuthority())
	{
		return 0;
	}
	const AFPSRLRoom* Room = nullptr;
	for (TActorIterator<AFPSRLRoom> It(World); It && !Room; ++It)
	{
		Room = It->IsInsideBounds(Boss->GetActorLocation()) ? *It : nullptr;
	}
	const FVector Center = Boss->GetActorLocation();
	const float Floor = Center.Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const TArray<AFPSRLShieldPlatform*> Platforms = AFPSRLShieldPlatform::FindAround(World, Center, 6000.f);
	// Every throw is the full set (it's the warning that a pull is coming): the oldest mines go to stay within the cap.
	const int32 Count = FMath::Clamp(Settings.MinesPerThrow, 0, Settings.MaxActiveMines);
	TArray<AFPSRLLandmine*> Old = GetMinesOf(Boss);
	Old.Sort([](const AFPSRLLandmine& A, const AFPSRLLandmine& B) { return A.GetGameTimeSinceCreation() > B.GetGameTimeSinceCreation(); });
	for (int32 Index = 0; Index < Old.Num() + Count - Settings.MaxActiveMines && Index < Old.Num(); ++Index)
	{
		Old[Index]->bDetonated = true;	// out of the count now (never goes off)
		Old[Index]->Destroy();
	}
	TArray<FVector> Taken;
	for (const AFPSRLLandmine* Mine : GetMinesOf(Boss))
	{
		Taken.Add(Mine->Destination);
	}
	auto IsFree = [&](const FVector& Spot)
	{
		bool bOk = Spot.Z - Floor < 200.f && FVector::Dist2D(Spot, Center) >= Settings.MinDistance * 0.8f && (!Room || Room->IsInsideBounds(Spot));
		for (const AFPSRLShieldPlatform* Platform : Platforms)
		{
			const FVector Local = Platform->GetActorTransform().InverseTransformPosition(Spot);
			bOk &= !(FMath::Abs(Local.X) <= Platform->HalfSize.X + 100.f && FMath::Abs(Local.Y) <= Platform->HalfSize.Y + 100.f);
		}
		for (const FVector& Other : Taken)
		{
			bOk &= FVector::Dist2D(Spot, Other) > Settings.TriggerRadius * 2.f + 50.f;
		}
		for (TActorIterator<APawn> It(World); It && bOk; ++It)
		{
			bOk &= !(UFPSRLHealthComponent::IsPlayerSide(nullptr, *It) && FVector::Dist2D(It->GetActorLocation(), Spot) < Settings.TriggerRadius + 250.f);
		}
		return bOk;
	};
	// Spread all round it: one direction per mine (a random start, jittered), each at its own random distance.
	int32 Thrown = 0;
	const float Start = FMath::FRandRange(0.f, 360.f);
	const FVector From = Center + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.6f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		for (int32 Try = 0; Try < 10; ++Try)
		{
			const float Yaw = Start + 360.f * Index / FMath::Max(1, Count) + FMath::FRandRange(-20.f, 20.f);
			const FVector Wanted = Center + FRotator(0.f, Yaw, 0.f).Vector() * FMath::FRandRange(Settings.MinDistance, Settings.MaxDistance);
			FNavLocation Point;
			if (!Nav->ProjectPointToNavigation(FVector(Wanted.X, Wanted.Y, Floor), Point, FVector(200.f, 200.f, 300.f)) || !IsFree(Point.Location))
			{
				continue;
			}
			if (Spawn(Boss, Point.Location, Settings.TriggerRadius, Settings.ExplosionRadius, Settings.Damage, Settings.ArmSeconds, From, Settings.ThrowSeconds))
			{
				Taken.Add(Point.Location);
				++Thrown;
			}
			break;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] %s throws %d mine(s) all round it, %d down now (max %d)"), *Boss->GetName(), Thrown, Taken.Num(), Settings.MaxActiveMines);
	return Thrown;
}
