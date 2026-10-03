// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "InputCoreTypes.h"
#include "FPSRLUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FFPSRLUserSettingsChanged);

/** How this player's microphone is sent: always (voice activity decides when) or only while the Push-to-Talk key is held. */
UENUM()
enum class EFPSRLVoiceInputMode : uint8
{
	OpenMic,
	PushToTalk
};

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

	// --- Voice chat (applied by UFPSRLVoiceSubsystem) ---

	/** Voice chat on: send this player's microphone and play teammates' voices. Off stays in the session, silent. */
	UPROPERTY(Config)
	bool bVoiceChatEnabled = true;

	/** Incoming teammate voices, 0-2 (separate from every other sound; above 1 boosts quiet voices). */
	UPROPERTY(Config)
	float VoiceChatVolume = 1.f;

	/** Outgoing microphone level, 0-2 (1 = unchanged). */
	UPROPERTY(Config)
	float MicrophoneVolume = 1.f;

	/** Open mic by default (user); Push-to-Talk is optional. */
	UPROPERTY(Config)
	EFPSRLVoiceInputMode VoiceInputMode = EFPSRLVoiceInputMode::OpenMic;

	/** The Push-to-Talk key (V by default, user; keyboard and mouse only). */
	UPROPERTY(Config)
	FKey PushToTalkKey = EKeys::V;

	/** Open mic: how quiet a voice still counts as talking (0-1; higher picks up quieter voices and more noise). The
	 *  engine's voice activity threshold is derived from it (UFPSRLVoiceSubsystem). Push-to-Talk sends everything. */
	UPROPERTY(Config)
	float OpenMicSensitivity = 0.7f;

	/** Microphone for voice chat (its name as the device list shows it); empty = the Windows default recording device. */
	UPROPERTY(Config)
	FString MicrophoneDevice;

	/** Output device for teammate voices (its name as the audio system lists it); empty = the game's own output. */
	UPROPERTY(Config)
	FString VoiceOutputDevice;

	static void SetVoiceChatEnabled(bool bEnabled);
	static void SetVoiceChatVolume(float Volume);
	static void SetMicrophoneVolume(float Volume);
	static void SetVoiceInputMode(EFPSRLVoiceInputMode Mode);
	static void SetPushToTalkKey(const FKey& Key);
	static void SetVoiceOutputDevice(const FString& DeviceName);
	static void SetOpenMicSensitivity(float Sensitivity);
	static void SetMicrophoneDevice(const FString& DeviceName);

	/** Writes pending changes now (they are saved half a second after the last change; closing Settings flushes). */
	static void FlushPendingSave();

	/** Fired after any setting changes. */
	static FFPSRLUserSettingsChanged OnChanged;
};
