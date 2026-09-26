// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLProjectile.generated.h"

/**
 * Network base for projectiles (parent of BP_ShooterProjectileBase; the Blueprint keeps its components, flight and
 * hit logic).
 *
 * Weapons are local actors on every machine (each machine equips every pawn itself), so a weapon spawns its projectile
 * on whichever machine pressed the trigger:
 *  - Server (listen host, enemy AI): the projectile is authoritative, replicates, and every client sees it.
 *  - Client: the local copy is cosmetic (instant feedback, its damage is ignored since clients never apply damage).
 *    Its BeginPlay asks the server to fire the real one (AFPSRLPlayerController::ServerFireProjectile), which is
 *    replicated to everyone except that client, who already sees their own copy.
 * A downed (or dead) shooter's projectiles are removed on spawn, on both sides.
 */
UCLASS(Abstract)
class FPSRL_API AFPSRLProjectile : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLProjectile();

	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	/** Can this pawn fire at all right now? (Players: not downed, not dead.) */
	static bool CanPawnShoot(const APawn* Pawn);

	/** The projectile's spawn-time settings (Expose on Spawn properties) as text, to rebuild it on the server. */
	TArray<FString> ExportSpawnSettings() const;
	void ImportSpawnSettings(const TArray<FString>& Settings);

	/** Server: fired on behalf of a client that already shows its own copy; not replicated to that client. */
	bool bHiddenFromInstigator = false;

protected:
	virtual void BeginPlay() override;
};
