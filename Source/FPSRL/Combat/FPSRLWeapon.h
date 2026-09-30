// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLWeapon.generated.h"

class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;

/**
 * A held gun (parent of BP_ShooterWeaponBase; Pistol / Rifle / Grenade Launcher are data-only children). Used by players
 * and enemies alike. The Blueprint keeps the meshes (FP_Weapon with its "Muzzle" socket, TP_Weapon); everything else is here.
 *
 * Firing: StartFiring / StopFiring. Full-auto weapons refire on a timer while held; semi-auto ones tell the holder when the
 * next shot is allowed (OnSemiWeaponRefire). Each shot spawns BulletClass from the muzzle toward the holder's aim point
 * (GetWeaponTargetLocation) with a random spread of AimVariance, which grows per shot and recovers while not firing. The
 * projectile network rules live in AFPSRLProjectile (a client's shot is cosmetic, the server fires the real one).
 *
 * The holder (player or enemy character Blueprint) implements BPI_WeaponHolder; this weapon calls those functions on its
 * owner by name: AttachWeaponMeshes, PlayFiringMontage, AddWeaponRecoil, UpdateWeaponHUD, GetWeaponTargetLocation,
 * OnWeaponActivated, OnWeaponDeactivated, OnSemiWeaponRefire.
 *
 * Aim-down-sights moves FP_Weapon between HipFire* and ADS* offsets; the weapon ticks only while that move or the spread
 * recovery is still settling (No-Tick policy: off the rest of the time).
 */
DECLARE_MULTICAST_DELEGATE(FFPSRLWeaponStateEvent);

UCLASS(Abstract, Blueprintable)
class FPSRL_API AFPSRLWeapon : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLWeapon();

	// --- Settings (per weapon Blueprint) ------------------------------------------------------------------------------

	/** What a shot spawns (bullet, grenade). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo")
	TSubclassOf<AActor> BulletClass;

	/** Damage of each shot (a grenade: its area damage), given to the projectile it spawns. 0 = the projectile's own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	float ShotDamage = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "1"))
	int32 MagSize = 10;

	/** Seconds from empty to full. 0 = instant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	float ReloadDuration = 0.f;

	/** Seconds between shots (hip fire; aiming is slower, see AimingRefireMultiplier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Refire", meta = (ClampMin = "0.01"))
	float BaseRefireRate = 0.5f;

	/** Keeps firing while the trigger is held; otherwise one shot per press. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Refire")
	bool bFullAuto = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Refire", meta = (ClampMin = "1"))
	float AimingRefireMultiplier = 1.6667f;

	/** Camera kick per shot (pitch; negative = up), scaled by ADSRecoilMultiplier while aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	float FiringRecoil = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	float ADSRecoilMultiplier = 0.2f;

	/** Spread (units around the aim point) at rest, its cap, how much each shot adds, and how fast it settles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float MinAimVariance = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float MaxAimVariance = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float AimVarianceGrowthPerShot = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float AimVarianceRecoverySpeed = 0.f;

	/** Resting spread while aiming, as a share of MinAimVariance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float ADSAimVarianceMultiplier = 0.f;

	/** FP_Weapon's offset from the hands at the hip and when aiming, and how fast it moves between them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	FVector HipFireLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	FRotator HipFireRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	FVector ADSLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim")
	FRotator ADSRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Aim", meta = (ClampMin = "0"))
	float WeaponAimInterpSpeed = 12.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	TObjectPtr<UAnimMontage> FiringMontage;

	/** The holder's arm / body animation while this weapon is out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstance;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstance;

	/** AI hearing: how loud a shot is and how far it carries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Perception")
	float ShotLoudness = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Perception")
	float ShotNoiseRange = 300.f;

	// --- Control (called by the holder) ---------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartFiring();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StopFiring();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StartAiming();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void StopAiming();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void Reload();

	/** Drawn: shown, noise tagged with OwnerTag (e.g. Player), holder told (OnWeaponActivated). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ActivateWeapon(FName OwnerTag);

	/** Put away: hidden, stops firing, holder told (OnWeaponDeactivated). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void DeactivateWeapon();

	// --- State --------------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsWeaponAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsWeaponFiring() const { return bIsFiring; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetCurrentBullets() const { return CurrentBullets; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetMagSize() const { return MagSize; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetAimVariance() const { return AimVariance; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetADSRecoilMultiplier() const { return ADSRecoilMultiplier; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	TSubclassOf<UAnimInstance> GetFirstPersonAnimInstance() const { return FirstPersonAnimInstance; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	TSubclassOf<UAnimInstance> GetThirdPersonAnimInstance() const { return ThirdPersonAnimInstance; }

	/** Seconds between shots right now (slower while aiming). */
	/** 0..1 through the current reload; 1 when not reloading. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadProgress() const;

	/** Ammo or reload state changed (HUD). Local: weapons are local actors on every machine. */
	FFPSRLWeaponStateEvent OnWeaponStateChanged;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetRefireRate() const;

	/** Seconds this reload takes (ReloadDuration / the holder's ReloadSpeedMultiplier). */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadDuration() const;

	/** Rounds left in the magazine. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 CurrentBullets = 0;

	/** Current spread around the aim point (grows per shot, settles back). Holders may adjust it (enemy accuracy). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Weapon|Aim")
	float AimVariance = 0.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The Blueprint's first-person mesh (with the "Muzzle" socket) and third-person mesh, found by name. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> FirstPersonWeaponMesh;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> ThirdPersonWeaponMesh;

private:
	void Fire();
	void FireShot(const FVector& Target);
	FTransform CalculateShotTransform(const FVector& Target) const;
	void HandleSemiRefireReady();
	void FinishReload();
	void WakeTick();

	UFUNCTION()
	void HandleOwnerDestroyed(AActor* DestroyedActor);

	/** Call a BPI_WeaponHolder function on the owner by name (Blueprint interface; name with or without spaces).
	 *  Arguments are assigned in parameter order. Returns the vector result for GetWeaponTargetLocation. */
	bool CallHolder(const TCHAR* FunctionName, TFunctionRef<void(UFunction*, uint8*)> SetArgs, FVector* OutVector = nullptr) const;

	bool bIsFiring = false;
	bool bIsAiming = false;
	bool bIsReloading = false;
	double TimeOfLastShot = -1000.0;
	double ReloadStartTime = 0.0;
	float CurrentReloadDuration = 0.f;

	/** A player holder's combat stat (Blessings, relics); 1 for enemies. */
	float GetHolderStat(const struct FGameplayAttribute& Attribute) const;
	FName NoiseTag;
	TWeakObjectPtr<APawn> PawnOwner;
	FTimerHandle RefireTimer;
	FTimerHandle ReloadTimer;
};
