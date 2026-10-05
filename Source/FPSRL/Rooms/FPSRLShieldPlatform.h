// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLShieldPlatform.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * One of the Ground Juggernaut arena's four permanent elevated platforms (the geometry is ordinary level meshes; this
 * actor sits on the platform's top, centred). It never moves or goes away. The shield mechanic
 * (UFPSRLShieldEncounterComponent) highlights the ones it needs - a tall cyan beam and a glowing rim, seen from anywhere
 * in the arena - and asks whether a player stands on it.
 *
 * Highlight replicated; standing checks on the server (no Tick, no overlaps: the mechanic asks a few times a second).
 */
UCLASS()
class FPSRL_API AFPSRLShieldPlatform : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLShieldPlatform();

	/** Half the platform top's size (cm), set by the arena builder to match the geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Platform")
	FVector2D HalfSize = FVector2D(350.f, 350.f);

	/** Shown in logs and tests ("North", "SW"...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Platform")
	FName PlatformName;

	/** Server: highlight it (or not). */
	void SetHighlighted(bool bInHighlighted);
	bool IsHighlighted() const { return bHighlighted; }

	/** A pawn standing on its top (its feet on it, not jumping far above it). */
	bool IsStandingOn(const APawn* Pawn) const;

	/** Platforms of the arena around Location (within Radius). */
	static TArray<AFPSRLShieldPlatform*> FindAround(const UWorld* World, const FVector& Location, float Radius);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_Highlighted();

	UPROPERTY(ReplicatedUsing = OnRep_Highlighted)
	bool bHighlighted = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HighlightParts;
};
