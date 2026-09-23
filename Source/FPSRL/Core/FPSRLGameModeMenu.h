// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FPSRLGameModeMenu.generated.h"

/**
 * GameMode for the main Menu map. BP_GameModeMenu derives from this.
 *
 * The Menu must never be a listen server. With SteamSockets, a listening Menu holds Steam P2P port 17777, and
 * the Host flow's "open Lvl_Lobby?listen" then fails ("Already have a listen socket on P2P vport 17777"),
 * bouncing the player back to the Menu. This happens whenever the Menu is launched with ?Listen, e.g. from the
 * editor with Net Mode = "Play As Listen Server". So if the Menu finds itself listening, it stops.
 */
UCLASS()
class FPSRL_API AFPSRLGameModeMenu : public AGameModeBase
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
