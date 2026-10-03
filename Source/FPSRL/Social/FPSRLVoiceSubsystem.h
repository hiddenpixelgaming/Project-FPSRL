// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/VoiceChannel.h"
#include "Net/VoiceConfig.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FPSRLVoiceSubsystem.generated.h"

class AFPSRLPlayerState;
class APlayerController;
class IOnlineVoice;
class UAudioComponent;

/** A teammate (by PlayerId) started / stopped speaking, or was muted / unmuted on this machine. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FFPSRLVoicePlayerEvent, int32 /*PlayerId*/, bool /*bState*/);

/** Why voice is not working right now (shown on the Voice settings). */
enum class EFPSRLVoiceStatus : uint8
{
	Ready,				// in a session, microphone found
	TurnedOff,			// the player's Voice Chat setting
	NotInSession,		// solo / main menu: nothing to send
	NoMicrophone,		// no capture device (or Windows refused access): teammates can still be heard
	Unavailable			// no voice interface (online subsystem without voice, voice disabled in config)
};

/**
 * Session voice chat (party of up to 4), built on the online subsystem's voice interface (Steam's in packaged
 * builds; the Null subsystem with -nosteam). The engine does the transport: microphone capture with voice-activity
 * detection, Opus encoding, the voice channel of the game connection (the listen host forwards packets to the other
 * players), decoding and playback. Raw audio never touches gameplay replication, RPCs or Tick.
 *
 * This subsystem (one per game instance, so it outlives Depth travel) adds the game's rules on top:
 *  - Identity: every teammate's PlayerState (server-replicated UniqueId) is registered as a remote talker; voice
 *    events are mapped back to that PlayerState's PlayerId, the same key the teammate HUD uses. The server only
 *    accepts a voice packet whose sender is the connection's own player (UFPSRLVoiceChannel), so nobody can speak
 *    as somebody else.
 *  - Speaking state: the voice interface's talking-state events (no polling) -> OnSpeakingChanged(PlayerId) -> the
 *    teammate HUD turns that name yellow. Several teammates may speak at once.
 *  - Transmit: Voice Chat on and in a session, and either Open Mic (default; the capture's voice activity decides
 *    when packets go out) or the Push-to-Talk key held.
 *  - Local mute: per teammate, this machine only (their packets are dropped here); nobody else is affected.
 *  - Settings (UFPSRLUserSettings): voice on/off (off = stop sending and mute everyone locally, still in the session),
 *    voice volume (each teammate's voice audio component), microphone volume (voice.MicInputGain), output device
 *    (the voice interface's endpoint patch). The microphone is the system's default recording device: the engine's
 *    voice capture offers no device choice.
 */
UCLASS()
class FPSRL_API UFPSRLVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UFPSRLVoiceSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	EFPSRLVoiceStatus GetStatus() const;
	FText GetStatusText() const;
	bool IsTransmitting() const { return bTransmitting; }

	/** Teammate PlayerId speaking right now (as heard here: muted teammates never are). */
	bool IsSpeaking(int32 PlayerId) const { return SpeakingPlayers.Contains(PlayerId); }
	FFPSRLVoicePlayerEvent OnSpeakingChanged;

	/** This player's own microphone is picking up speech (the voice capture's activity detection; Settings shows it). */
	bool IsLocalSpeaking() const { return bLocalSpeaking; }
	FSimpleMulticastDelegate OnLocalSpeakingChanged;

	/** The voice activity threshold for open mic from the sensitivity setting (0.1 x (1 - sensitivity)^2, at least 0.001). */
	static float GetOpenMicThreshold();

	/** Local mute (this machine only; lasts for the session). */
	void SetMuted(int32 PlayerId, bool bMuted);
	bool IsMuted(int32 PlayerId) const { return MutedPlayers.Contains(PlayerId); }
	FFPSRLVoicePlayerEvent OnMuteChanged;

	/** The Push-to-Talk key went down / up (the player controller's input). */
	void SetPushToTalkHeld(bool bHeld);
	bool IsPushToTalkHeld() const { return bPushToTalkHeld; }

	/** Re-applies whether this player sends voice (settings, session, login and travel). */
	void RefreshTransmit();

	/** The session is over (main menu): stop sending, forget teammates, their speaking and mute states. */
	void ResetSession();

	/** Output devices teammates' voices can be sent to (names as the audio system lists them; empty without audio). */
	TArray<FString> GetOutputDevices() const;

	/** Test: "status Ready, transmitting 1, talkers 2, speaking [257], muted []". */
	FString Describe() const;

	/** A teammate's voice audio started / ended playing here (UFPSRLVoiceTalker): voice volume is applied to it. */
	void NotifyVoiceAudio(UAudioComponent* AudioComponent, bool bStarted);

private:
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> GetVoice() const;
	UWorld* GetGameWorld() const;
	APlayerController* GetLocalController() const;

	void HandleTalkingStateChanged(TSharedRef<const class FUniqueNetId> TalkerId, bool bIsTalking);
	void HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState);
	void HandlePlayerStateChanged(AFPSRLPlayerState* PlayerState);
	void HandlePlayerStateEnd(AFPSRLPlayerState* PlayerState, EEndPlayReason::Type Reason);
	void HandleSettingsChanged();
	void HandleSeamlessTravelStart(UWorld* World, const FString& LevelName);

	/** A teammate's PlayerState in the current world: register them with the voice interface, give them a talker. */
	void RegisterTeammate(AFPSRLPlayerState* PlayerState);
	void RegisterAllTeammates();
	bool IsRemotePlayer(const AFPSRLPlayerState* PlayerState) const;
	AFPSRLPlayerState* FindPlayerState(const FUniqueNetId& Id) const;

	void SetSpeaking(int32 PlayerId, bool bSpeaking);
	/** Voice off, or a teammate muted: the voice interface drops their packets here. */
	void ApplyIncoming();
	void ApplyVolumes();
	void ApplyOutputDevice();
	/** Open mic: the engine's activity threshold and noise gate from the sensitivity; Push-to-Talk: 0 (send all). */
	void ApplyCaptureThreshold();

	FDelegateHandle TalkingHandle;
	FDelegateHandle BeginHandle;
	FDelegateHandle ChangedHandle;
	FDelegateHandle EndHandle;
	FDelegateHandle SettingsHandle;
	FDelegateHandle TravelHandle;
	FDelegateHandle SessionCreatedHandle;
	FDelegateHandle SessionJoinedHandle;
	FDelegateHandle SessionEndedHandle;

	TSet<int32> SpeakingPlayers;
	TSet<int32> MutedPlayers;
	/** PlayerId -> online id, for every teammate registered as a remote talker. */
	TMap<int32, FUniqueNetIdRepl> Talkers;
	TArray<TWeakObjectPtr<UAudioComponent>> VoiceAudio;
	bool bTransmitting = false;
	bool bPushToTalkHeld = false;
	bool bLocalSpeaking = false;
	bool bLoggedLocalSpeech = false;
	/** Teammates heard at least once this session (logged the first time). */
	TSet<int32> HeardPlayers;
	FString AppliedOutputDevice;
	double LastTravelStart = -1.0e9;
};

/**
 * Per-teammate voice hook (a component on their PlayerState): the voice engine hands it the audio component that
 * plays that teammate's voice, so the Voice Chat Volume setting applies to voice only.
 */
UCLASS()
class FPSRL_API UFPSRLVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

private:
	TWeakObjectPtr<UAudioComponent> Playing;
};

/**
 * The game connection's voice channel, with one rule added on the server: a voice packet is accepted only when its
 * sender is the connection's own player, so a client can never speak as another player. Everything else is the
 * engine's channel (DefaultEngine.ini ChannelDefinitions points the Voice channel here).
 */
UCLASS(transient, customConstructor)
class FPSRL_API UFPSRLVoiceChannel : public UVoiceChannel
{
	GENERATED_BODY()

public:
	UFPSRLVoiceChannel(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
		: UVoiceChannel(ObjectInitializer)
	{
	}

protected:
	virtual void ReceivedBunch(FInBunch& Bunch) override;
};
