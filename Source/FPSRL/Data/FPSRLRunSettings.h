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

	/**
	 * Once the first player is in the portal, the rest of the living party has this long before everyone travels.
	 * 0 = wait until every living player is in.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0"))
	float PortalCountdownSeconds = 10.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
