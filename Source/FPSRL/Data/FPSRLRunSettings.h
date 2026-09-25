// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRLRunSettings.generated.h"

class UFPSRLRunDefinition;

/**
 * Run structure tuning. Edit in Project Settings > Game > FPSRL Run (saved to Config/DefaultGame.ini).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Run"))
class FPSRL_API UFPSRLRunSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UFPSRLRunSettings& Get() { return *GetDefault<UFPSRLRunSettings>(); }

	/** The expedition started by the Lobby's Start button. Unset = the Lobby's MatchMap is used as a one-Depth run. */
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	TSoftObjectPtr<UFPSRLRunDefinition> RunDefinition;

	/** Where the party returns when a run ends. */
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	TSoftObjectPtr<UWorld> LobbyMap;

	/** Seconds between the Depth completing and the portal accepting players (activation VFX window). */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0"))
	float PortalActivationDelay = 1.f;

	/** Share of living players choosing Continue that starts the countdown (0.5 = half or more). */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0.01", ClampMax = "1"))
	float PortalVoteThreshold = 0.5f;

	/**
	 * Once the threshold is met, the rest of the living party has this long before everyone is moved together.
	 * 0 = no countdown: wait until every living player chooses Continue.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0"))
	float PortalCountdownSeconds = 35.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
