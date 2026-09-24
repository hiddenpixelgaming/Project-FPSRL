// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRunSubsystem.generated.h"

class UFPSRLDepthDefinition;
class UFPSRLRunDefinition;

/**
 * Server-side run progress (which Depth of which Area the party is in). Lives on the GameInstance, so it survives
 * the map travel between Depths; each Depth's AFPSRLGameState mirrors the numbers to clients.
 *
 * Forward-only: StartRun -> AdvanceRun (from an exit portal) ... -> EndRun (back to the Lobby). There is no way to
 * travel to an earlier Depth. Player build state (boons, aspect, weapon) travels with each PlayerState
 * (AFPSRLPlayerState::CopyProperties), not here.
 *
 * Only the server's instance is meaningful; on clients it stays idle.
 */
UCLASS()
class FPSRL_API UFPSRLRunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Server: begins the run in Project Settings > FPSRL Run and travels to its first Depth. False if none is set. */
	bool StartRun(UWorld* World);

	/** Server: the party took the exit portal. Travels to the next Depth, or ends the run after the last one. */
	void AdvanceRun(UWorld* World);

	/** Server: run over (victory, wipe, abandon). Travels back to the Lobby. */
	void EndRun(UWorld* World, bool bVictory);

	UFUNCTION(BlueprintPure, Category = "Run")
	bool IsRunActive() const { return bRunActive; }

	/** 1-based numbers for display; 0 when no run is active (e.g. PIE started directly in a Depth map). */
	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetDepthNumber() const { return bRunActive ? DepthIndex + 1 : 0; }

	UFUNCTION(BlueprintPure, Category = "Run")
	int32 GetAreaNumber() const;

	const UFPSRLDepthDefinition* GetCurrentDepth() const;

	/** What the current Depth's portal leads to. RunComplete when no run is active. */
	EPortalDestination GetNextDestination() const;

private:
	bool TravelTo(UWorld* World, const TSoftObjectPtr<UWorld>& Map) const;

	UPROPERTY(Transient)
	TObjectPtr<const UFPSRLRunDefinition> Run;

	bool bRunActive = false;

	/** Flat index across Areas. */
	int32 DepthIndex = 0;
};
