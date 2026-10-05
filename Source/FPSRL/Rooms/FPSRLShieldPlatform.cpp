// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLShieldPlatform.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "FPSRL.h"

namespace FPSRLShieldPlatform
{
	static const TCHAR* CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* BeamMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyPhased.M_EnemyPhased");
	static const TCHAR* RimMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_PlatformHighlight.M_PlatformHighlight");
	static constexpr float BeamHeight = 3000.f;
	static constexpr float RimThickness = 30.f;
	static constexpr float StandingAbove = 120.f;	// feet up to this far above the top still count (small hops)
}

AFPSRLShieldPlatform::AFPSRLShieldPlatform()
{
	bReplicates = true;
	bAlwaysRelevant = true;	// seen from anywhere in the arena
	SetReplicateMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFPSRLShieldPlatform::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFPSRLShieldPlatform, bHighlighted);
}

void AFPSRLShieldPlatform::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	auto Make = [this](const TCHAR* Name, const TCHAR* MeshPath, const TCHAR* MaterialPath, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, Name);
		Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetupAttachment(RootComponent);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->RegisterComponent();
		HighlightParts.Add(Part);
	};
	// A tall beam over the platform (seen across the arena) and a glowing rim around its top.
	const float Beam = FMath::Min(HalfSize.X, HalfSize.Y) * 1.4f / 100.f;
	Make(TEXT("Beam"), FPSRLShieldPlatform::CylinderMesh, FPSRLShieldPlatform::BeamMaterial, FVector(0.f, 0.f, FPSRLShieldPlatform::BeamHeight * 0.5f),
		FVector(Beam, Beam, FPSRLShieldPlatform::BeamHeight / 100.f));
	const float T = FPSRLShieldPlatform::RimThickness;
	Make(TEXT("RimN"), FPSRLShieldPlatform::CubeMesh, FPSRLShieldPlatform::RimMaterial, FVector(HalfSize.X, 0.f, 5.f), FVector(T / 100.f, HalfSize.Y * 2.f / 100.f, 0.1f));
	Make(TEXT("RimS"), FPSRLShieldPlatform::CubeMesh, FPSRLShieldPlatform::RimMaterial, FVector(-HalfSize.X, 0.f, 5.f), FVector(T / 100.f, HalfSize.Y * 2.f / 100.f, 0.1f));
	Make(TEXT("RimE"), FPSRLShieldPlatform::CubeMesh, FPSRLShieldPlatform::RimMaterial, FVector(0.f, HalfSize.Y, 5.f), FVector(HalfSize.X * 2.f / 100.f, T / 100.f, 0.1f));
	Make(TEXT("RimW"), FPSRLShieldPlatform::CubeMesh, FPSRLShieldPlatform::RimMaterial, FVector(0.f, -HalfSize.Y, 5.f), FVector(HalfSize.X * 2.f / 100.f, T / 100.f, 0.1f));
	OnRep_Highlighted();
}

void AFPSRLShieldPlatform::SetHighlighted(bool bInHighlighted)
{
	if (HasAuthority() && bHighlighted != bInHighlighted)
	{
		bHighlighted = bInHighlighted;
		OnRep_Highlighted();
		ForceNetUpdate();
	}
}

void AFPSRLShieldPlatform::OnRep_Highlighted()
{
	UE_LOG(LogFPSRL, Verbose, TEXT("[Platform] %s %s (%s)"), *PlatformName.ToString(), bHighlighted ? TEXT("lit") : TEXT("dark"), HasAuthority() ? TEXT("server") : TEXT("client"));
	for (UStaticMeshComponent* Part : HighlightParts)
	{
		if (Part)
		{
			Part->SetVisibility(bHighlighted);
		}
	}
}

bool AFPSRLShieldPlatform::IsStandingOn(const APawn* Pawn) const
{
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Character->GetActorLocation());
	const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - GetActorLocation().Z;
	return FMath::Abs(Local.X) <= HalfSize.X && FMath::Abs(Local.Y) <= HalfSize.Y && Feet > -30.f && Feet < FPSRLShieldPlatform::StandingAbove;
}

TArray<AFPSRLShieldPlatform*> AFPSRLShieldPlatform::FindAround(const UWorld* World, const FVector& Location, float Radius)
{
	TArray<AFPSRLShieldPlatform*> Found;
	for (TActorIterator<AFPSRLShieldPlatform> It(World); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), Location) <= Radius)
		{
			Found.Add(*It);
		}
	}
	Found.Sort([](const AFPSRLShieldPlatform& A, const AFPSRLShieldPlatform& B) { return A.PlatformName.LexicalLess(B.PlatformName); });
	return Found;
}
