// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLWeapon.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetMathLibrary.h"
#include "TimerManager.h"
#include "FPSRL.h"

AFPSRLWeapon::AFPSRLWeapon()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;	// only while the aim move or spread recovery is settling
}

void AFPSRLWeapon::BeginPlay()
{
	Super::BeginPlay();

	TInlineComponentArray<USkeletalMeshComponent*> Meshes(this);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetName().StartsWith(TEXT("FP_Weapon")))
		{
			FirstPersonWeaponMesh = Mesh;
		}
		else if (Mesh->GetName().StartsWith(TEXT("TP_Weapon")))
		{
			ThirdPersonWeaponMesh = Mesh;
		}
	}
	CurrentBullets = MagSize;
	AimVariance = MinAimVariance;

	AActor* Holder = GetOwner();
	PawnOwner = Cast<APawn>(Holder);
	if (Holder)
	{
		Holder->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleOwnerDestroyed);
		CallHolder(TEXT("AttachWeaponMeshes"), [this](UFunction* Function, uint8* Params)
		{
			// (Weapon, First Person Mesh, Third Person Mesh)
			int32 Index = 0;
			for (TFieldIterator<FObjectPropertyBase> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
			{
				UObject* Value = Index == 0 ? static_cast<UObject*>(this) : Index == 1 ? static_cast<UObject*>(FirstPersonWeaponMesh) : static_cast<UObject*>(ThirdPersonWeaponMesh);
				It->SetObjectPropertyValue_InContainer(Params, Value);
			}
		});
	}
}

void AFPSRLWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RefireTimer);
	GetWorldTimerManager().ClearTimer(ReloadTimer);
	Super::EndPlay(EndPlayReason);
}

void AFPSRLWeapon::HandleOwnerDestroyed(AActor* DestroyedActor)
{
	Destroy();
}

// --- Firing ------------------------------------------------------------------------------------------------------------

void AFPSRLWeapon::StartFiring()
{
	if (bIsReloading)
	{
		return;
	}
	bIsFiring = true;
	WakeTick();
	const double SinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	const float Refire = GetRefireRate();
	if (SinceLastShot > Refire)
	{
		Fire();
	}
	else if (bFullAuto)
	{
		// Pressed again mid-cooldown: fire as soon as the cooldown allows.
		GetWorldTimerManager().SetTimer(RefireTimer, this, &ThisClass::Fire, static_cast<float>(Refire - SinceLastShot), false);
	}
}

void AFPSRLWeapon::StopFiring()
{
	bIsFiring = false;
	GetWorldTimerManager().ClearTimer(RefireTimer);
	WakeTick();	// spread starts recovering
}

void AFPSRLWeapon::Fire()
{
	if (!bIsFiring || bIsReloading)
	{
		return;
	}
	FVector Target = GetActorLocation() + GetActorForwardVector() * 10000.f;
	CallHolder(TEXT("GetWeaponTargetLocation"), [](UFunction*, uint8*) {}, &Target);
	FireShot(Target);

	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	AimVariance = FMath::Clamp(AimVariance + AimVarianceGrowthPerShot, 0.f, FMath::Max(MaxAimVariance, MinAimVariance));
	if (APawn* Pawn = PawnOwner.Get())
	{
		MakeNoise(ShotLoudness, Pawn, GetOwner()->GetActorLocation(), ShotNoiseRange, NoiseTag);
	}

	if (bFullAuto)
	{
		GetWorldTimerManager().SetTimer(RefireTimer, this, &ThisClass::Fire, GetRefireRate(), false);
	}
	else
	{
		GetWorldTimerManager().SetTimer(RefireTimer, this, &ThisClass::HandleSemiRefireReady, GetRefireRate(), false);
	}
}

void AFPSRLWeapon::HandleSemiRefireReady()
{
	CallHolder(TEXT("OnSemiWeaponRefire"), [](UFunction*, uint8*) {});
}

FTransform AFPSRLWeapon::CalculateShotTransform(const FVector& Target) const
{
	// From just in front of the muzzle toward the aim point, off by up to AimVariance in a random direction.
	const FVector Muzzle = FirstPersonWeaponMesh ? FirstPersonWeaponMesh->GetSocketLocation(TEXT("Muzzle")) : GetActorLocation();
	const FVector Start = Muzzle + UKismetMathLibrary::FindLookAtRotation(Muzzle, Target).Vector() * 10.f;
	const FVector Aim = Target + FMath::VRand() * AimVariance;
	return FTransform(UKismetMathLibrary::FindLookAtRotation(Start, Aim), Start);
}

void AFPSRLWeapon::FireShot(const FVector& Target)
{
	if (BulletClass)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.Instigator = PawnOwner.Get();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AActor>(BulletClass, CalculateShotTransform(Target), Params);
	}
	if (FiringMontage)
	{
		CallHolder(TEXT("PlayFiringMontage"), [this](UFunction* Function, uint8* Params)
		{
			if (FObjectPropertyBase* Montage = CastField<FObjectPropertyBase>(Function->ChildProperties))
			{
				Montage->SetObjectPropertyValue_InContainer(Params, FiringMontage);
			}
		});
	}
	CallHolder(TEXT("AddWeaponRecoil"), [this](UFunction* Function, uint8* Params)
	{
		if (FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Function->ChildProperties))
		{
			AsDouble->SetPropertyValue_InContainer(Params, FiringRecoil);
		}
		else if (FFloatProperty* AsFloat = CastField<FFloatProperty>(Function->ChildProperties))
		{
			AsFloat->SetPropertyValue_InContainer(Params, FiringRecoil);
		}
	});

	--CurrentBullets;
	OnWeaponStateChanged.Broadcast();
	if (CurrentBullets <= 0)
	{
		Reload();
	}
	CallHolder(TEXT("UpdateWeaponHUD"), [this](UFunction* Function, uint8* Params)
	{
		// (Current Bullets, Magazine Size)
		int32 Index = 0;
		for (TFieldIterator<FIntProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
		{
			It->SetPropertyValue_InContainer(Params, Index == 0 ? CurrentBullets : MagSize);
		}
	});
}

void AFPSRLWeapon::Reload()
{
	if (bIsReloading || CurrentBullets >= MagSize)
	{
		return;
	}
	bIsReloading = true;
	StopFiring();
	ReloadStartTime = GetWorld()->GetTimeSeconds();
	OnWeaponStateChanged.Broadcast();
	if (ReloadDuration > 0.f)
	{
		GetWorldTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishReload, ReloadDuration, false);
	}
	else
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::FinishReload);
	}
}

void AFPSRLWeapon::FinishReload()
{
	CurrentBullets = MagSize;
	bIsReloading = false;
	OnWeaponStateChanged.Broadcast();
	CallHolder(TEXT("UpdateWeaponHUD"), [this](UFunction* Function, uint8* Params)
	{
		int32 Index = 0;
		for (TFieldIterator<FIntProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
		{
			It->SetPropertyValue_InContainer(Params, Index == 0 ? CurrentBullets : MagSize);
		}
	});
}

// --- Aiming, drawing ---------------------------------------------------------------------------------------------------

void AFPSRLWeapon::StartAiming()
{
	bIsAiming = true;
	WakeTick();
}

void AFPSRLWeapon::StopAiming()
{
	bIsAiming = false;
	WakeTick();
}

void AFPSRLWeapon::ActivateWeapon(FName OwnerTag)
{
	NoiseTag = OwnerTag;
	SetActorHiddenInGame(false);
	OnWeaponStateChanged.Broadcast();
	CallHolder(TEXT("OnWeaponActivated"), [this](UFunction* Function, uint8* Params)
	{
		if (FObjectPropertyBase* Weapon = CastField<FObjectPropertyBase>(Function->ChildProperties))
		{
			Weapon->SetObjectPropertyValue_InContainer(Params, this);
		}
	});
}

void AFPSRLWeapon::DeactivateWeapon()
{
	SetActorHiddenInGame(true);
	StopFiring();
	CallHolder(TEXT("OnWeaponDeactivated"), [this](UFunction* Function, uint8* Params)
	{
		if (FObjectPropertyBase* Weapon = CastField<FObjectPropertyBase>(Function->ChildProperties))
		{
			Weapon->SetObjectPropertyValue_InContainer(Params, this);
		}
	});
}

void AFPSRLWeapon::WakeTick()
{
	SetActorTickEnabled(true);
}

void AFPSRLWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Weapon slides between the hip and aim-down-sights offsets.
	const FVector TargetLocation = bIsAiming ? ADSLocation : HipFireLocation;
	const FRotator TargetRotation = bIsAiming ? ADSRotation : HipFireRotation;
	bool bSettled = true;
	if (FirstPersonWeaponMesh)
	{
		const FVector Location = FMath::VInterpTo(FirstPersonWeaponMesh->GetRelativeLocation(), TargetLocation, DeltaSeconds, WeaponAimInterpSpeed);
		const FRotator Rotation = FMath::RInterpTo(FirstPersonWeaponMesh->GetRelativeRotation(), TargetRotation, DeltaSeconds, WeaponAimInterpSpeed);
		FirstPersonWeaponMesh->SetRelativeLocationAndRotation(Location, Rotation);
		bSettled = FVector::Dist(Location, TargetLocation) < 0.5f && Rotation.Equals(TargetRotation, 0.5f);
	}

	// Spread settles back while not firing (tighter while aiming).
	const float RestingVariance = bIsAiming ? MinAimVariance * ADSAimVarianceMultiplier : MinAimVariance;
	if (!bIsFiring)
	{
		AimVariance = FMath::FInterpTo(AimVariance, RestingVariance, DeltaSeconds, AimVarianceRecoverySpeed);
	}
	bSettled &= FMath::IsNearlyEqual(AimVariance, RestingVariance, 0.5f);

	if (bSettled && !bIsFiring)
	{
		SetActorTickEnabled(false);
	}
}

// --- Holder calls ------------------------------------------------------------------------------------------------------

bool AFPSRLWeapon::CallHolder(const TCHAR* FunctionName, TFunctionRef<void(UFunction*, uint8*)> SetArgs, FVector* OutVector) const
{
	AActor* Holder = GetOwner();
	if (!Holder)
	{
		return false;
	}
	// Blueprint interface functions keep their display names ("Attach Weapon Meshes"); try that form and the compact one.
	UFunction* Function = Holder->FindFunction(FunctionName);
	if (!Function)
	{
		FString Spaced;
		for (const TCHAR* Char = FunctionName; *Char; ++Char)
		{
			if (FChar::IsUpper(*Char) && Char != FunctionName && FChar::IsLower(*(Char - 1)))	// "UpdateWeaponHUD" -> "Update Weapon HUD"
			{
				Spaced.AppendChar(TEXT(' '));
			}
			Spaced.AppendChar(*Char);
		}
		Function = Holder->FindFunction(*Spaced);
	}
	if (!Function)
	{
		return false;	// this holder doesn't implement it (fine: the interface is optional)
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment()));
	FMemory::Memzero(Params, Function->ParmsSize);
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Params);
	}
	SetArgs(Function, Params);
	Holder->ProcessEvent(Function, Params);
	if (OutVector)
	{
		for (TFieldIterator<FStructProperty> It(Function); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) && It->Struct == TBaseStructure<FVector>::Get())
			{
				*OutVector = *It->ContainerPtrToValuePtr<FVector>(Params);
			}
		}
	}
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Params);
	}
	return true;
}

float AFPSRLWeapon::GetReloadProgress() const
{
	if (!bIsReloading || ReloadDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - ReloadStartTime) / ReloadDuration), 0.f, 1.f);
}
