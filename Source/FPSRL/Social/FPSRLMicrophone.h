// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class IVoiceCapture;
class IVoiceEncoder;

/** What one capture step produced. */
struct FFPSRLMicStep
{
	/** Opus data ready to send (empty: nothing to send this step). */
	TArray<uint8> Encoded;
	/** Stream position of that data (receivers line packets up by it; continuous over what we send). */
	uint64 SampleCount = 0;
	/** Level after gain, RMS 0-1 (the Settings meter). */
	float Level = 0.f;
	/** Voice activity right now (with a short hold after the last voiced frame). */
	bool bVoice = false;
};

/**
 * This player's microphone for voice chat, replacing the engine's own capture so the player can choose the device
 * (the engine's voice capture always opens the Windows default recording device; the v0.1.38 playtest's teammate
 * talked into a different one). Uses the engine's voice capture on the chosen device and its Opus encoder, so the
 * packets are exactly what the engine sends and every receiver decodes them with the engine's normal path.
 *
 * On top of the raw capture: voice activity detection (a level threshold from the open mic sensitivity, raised over an
 * adaptive noise floor), automatic gain (quiet microphones are brought up to a common speech level, loud ones down;
 * a soft limiter stops clipping) and the player's microphone volume as a trim. Auto-detect listens to every input at
 * once for a few seconds and picks the one that hears the player.
 */
class FPSRL_API FFPSRLMicrophone
{
public:
	~FFPSRLMicrophone();

	/** Recording devices by name (as the voice capture opens them). */
	static TArray<FString> GetDevices();
	/** The Windows default recording device's name ("" when unknown). */
	static FString GetDefaultDeviceName();

	/** Opens a device ("" = the Windows default). False when it cannot be opened (no device, access refused). */
	bool Open(const FString& InDeviceName);
	void Close();
	bool IsOpen() const { return Capture.IsValid(); }
	const FString& GetDeviceName() const { return DeviceName; }
	/** The automatic gain right now (1 = unchanged). */
	float GetGain() const { return Gain; }
	/** No audio from the device for a while (it stalled or was taken away): the owner reopens it. */
	bool IsStalled() const;
	/** Packets sent and bytes, for the voice stats log. */
	int32 GetPacketsSent() const { return PacketsSent; }
	int64 GetBytesSent() const { return BytesSent; }

	/**
	 * Reads what was captured since the last step and processes it (activity, automatic gain, level). Encodes it when
	 * bSendAlways (Push-to-Talk held) or bSendWhenVoice and voice is active (open mic).
	 */
	void Step(float OpenThreshold, float Trim, bool bSendWhenVoice, bool bSendAlways, FFPSRLMicStep& Out);

	/** Auto-detect: listen to every device for Seconds. */
	void StartDetect(float Seconds);
	bool IsDetecting() const { return !Probes.IsEmpty(); }
	/** One detect step; true when finished: OutDevice = the device that heard speech best ("" = none did). */
	bool StepDetect(FString& OutDevice, float& OutPeak);

private:
	/** Voice level a speaking player is brought to (RMS, about -22 dBFS). */
	static constexpr float TargetLevel = 0.14f;	// about -17 dBFS (v0.1.39 playtest: 0.08 was too soft)
	static constexpr float MaxGain = 16.f;
	static constexpr double VoiceHoldSeconds = 0.4;

	TSharedPtr<IVoiceCapture> Capture;
	TSharedPtr<IVoiceEncoder> Encoder;
	FString DeviceName;
	TArray<uint8> Pcm;
	TArray<uint8> Unencoded;
	TArray<uint8> Compressed;
	float Gain = 2.f;
	float NoiseFloor = 0.003f;
	float SmoothedLevel = 0.f;
	double LastVoiceTime = -100.0;
	/** When the device last delivered audio. */
	double LastDataTime = 0.0;
	/** Sample position of the next audio we send: advances only with audio actually sent, so receivers never queue
	 *  silence for our pauses and a reopened device continues the same stream. */
	uint64 SentSamples = 0;
	int32 PacketsSent = 0;
	int64 BytesSent = 0;

	struct FProbe
	{
		FString Name;
		TSharedPtr<IVoiceCapture> Capture;
		float Peak = 0.f;
	};
	TArray<FProbe> Probes;
	double DetectEndTime = 0.0;
};
