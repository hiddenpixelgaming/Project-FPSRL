// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FPSRLUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FFPSRLUserSettingsChanged);

/**
 * The player's own preferences (this machine only), saved to the user settings file (Saved/Config/<Platform>/
 * GameUserSettings.ini). Edited from the pause menu's Settings screen. The start of the settings menu: more options
 * join here as they are added.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class FPSRL_API UFPSRLUserSettings : public UObject
{
	GENERATED_BODY()

public:
	static const UFPSRLUserSettings* Get() { return GetDefault<UFPSRLUserSettings>(); }

	/** Mask profanity in chat messages shown to this player (others are unaffected). */
	UPROPERTY(Config)
	bool bFilterProfanity = true;

	/** Change the profanity filter, save, and tell listeners (the chat redraws its lines). */
	static void SetFilterProfanity(bool bEnabled);

	/** Fired after any setting changes. */
	static FFPSRLUserSettingsChanged OnChanged;
};
