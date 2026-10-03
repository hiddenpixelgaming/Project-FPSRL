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
	/** The capture's sample counter for that data (receivers use it to line packets up). */
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
	static constexpr float TargetLevel = 0.08f;
	static constexpr float MaxGain = 12.f;
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

	struct FProbe
	{
		FString Name;
		TSharedPtr<IVoiceCapture> Capture;
		float Peak = 0.f;
	};
	TArray<FProbe> Probes;
	double DetectEndTime = 0.0;
};
