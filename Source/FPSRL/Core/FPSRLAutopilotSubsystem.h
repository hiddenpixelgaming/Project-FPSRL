// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "FPSRLAutopilotSubsystem.generated.h"

/**
 * Test only (does nothing in Shipping): plays a whole run by itself on the listen host, for headless checks of the
 * Depth / room flow. Started with the FPSRLAutoRun console command in the Lobby. Once a second it starts the run, moves
 * the host into the next encounter room once it is loaded everywhere, starts and clears the encounter, and votes at the
 * exit portal, logging the Depth state as it changes ("[Autopilot]"). Quits the game when the run is back in the Lobby,
 * or after MaxSteps. Lives on the GameInstance so it survives the travel between Depths.
 */
UCLASS()
class FPSRL_API UFPSRLAutopilotSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** bTestFall: once in the first encounter room, drop the host off the side of the level (checks the fall volume). */
	void Start(bool bTestFall = false, int32 InMinPlayers = 1);

	/** Once MinPlayers are in the Lobby: open a Blessing altar for every player 40 times (plus rerolls) and log how often
	 *  two players got the identical Aspect set, or a reroll repeated or mirrored the set before it. Then quits. */
	void StartAltarTest(int32 InMinPlayers);

	virtual void Deinitialize() override;

private:
	bool Step(float DeltaTime);
	void Finish(const TCHAR* Result);
	void RunAltarTest();

	FTSTicker::FDelegateHandle TickerHandle;
	bool bStartedRun = false;
	bool bFallPending = false;
	bool bAltarTest = false;
	int32 MinPlayers = 1;
	int32 Steps = 0;
	int32 MaxSteps = 900;
	FString LastStatus;
};
