// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLMicrophone.h"
#include "AudioCaptureCore.h"
#include "Interfaces/VoiceCapture.h"
#include "Interfaces/VoiceCodec.h"
#include "Net/VoiceConfig.h"
#include "VoiceModule.h"
#include "FPSRL.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmsystem.h>
#include <dsound.h>
#include "Windows/HideWindowsPlatformTypes.h"
// mmsystem.h defines PlaySound (and friends) as macros; unity builds merge this file with others that call
// FPSRLCombatFeedback::PlaySound.
#undef PlaySound
#endif

namespace FPSRLMicrophone
{
#if PLATFORM_WINDOWS
	static BOOL CALLBACK AddCaptureDevice(LPGUID Guid, LPCWSTR Description, LPCWSTR Module, LPVOID Context)
	{
		// The first entry (no GUID) is "Primary Sound Capture Driver", i.e. the Windows default: offered separately.
		if (Guid && Description && Context)
		{
			static_cast<TArray<FString>*>(Context)->AddUnique(FString(Description));
		}
		return 1;
	}
#endif

	/** RMS of int16 samples, 0-1. */
	static float Rms(const int16* Samples, int32 Count)
	{
		double Sum = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Value = Samples[Index] / 32768.0;
			Sum += Value * Value;
		}
		return Count > 0 ? static_cast<float>(FMath::Sqrt(Sum / Count)) : 0.f;
	}
}

FFPSRLMicrophone::~FFPSRLMicrophone()
{
	Close();
	for (FProbe& Probe : Probes)
	{
		if (Probe.Capture)
		{
			Probe.Capture->Shutdown();
		}
	}
}

TArray<FString> FFPSRLMicrophone::GetDevices()
{
	TArray<FString> Names;
#if PLATFORM_WINDOWS
	DirectSoundCaptureEnumerateW(&FPSRLMicrophone::AddCaptureDevice, &Names);
#endif
	return Names;
}

FString FFPSRLMicrophone::GetDefaultDeviceName()
{
	Audio::FAudioCapture AudioCapture;
	Audio::FCaptureDeviceInfo Info;
	return AudioCapture.GetCaptureDeviceInfo(Info, INDEX_NONE) ? Info.DeviceName : FString();
}

bool FFPSRLMicrophone::Open(const FString& InDeviceName)
{
	Close();
	FVoiceModule& Voice = FVoiceModule::Get();
	if (!Voice.IsVoiceEnabled())
	{
		return false;
	}
	Capture = Voice.CreateVoiceCapture(InDeviceName);
	Encoder = Voice.CreateVoiceEncoder();
	if (!Capture || !Encoder || !Capture->Start())
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Voice] could not open the microphone '%s'"), InDeviceName.IsEmpty() ? TEXT("Windows default") : *InDeviceName);
		Close();
		return false;
	}
	// Better than the engine's own sender (complexity 1, Opus default bitrate): full complexity, 32 kbit/s variable.
	Encoder->SetComplexity(10);
	Encoder->SetBitrate(32000);
	Encoder->SetVBR(true);
	LastDataTime = FPlatformTime::Seconds();
	DeviceName = InDeviceName;
	Gain = 2.f;
	NoiseFloor = 0.003f;
	SmoothedLevel = 0.f;
	LastVoiceTime = -100.0;
	UE_LOG(LogFPSRL, Log, TEXT("[Voice] microphone open: %s"), InDeviceName.IsEmpty() ? *FString::Printf(TEXT("Windows default (%s)"), *GetDefaultDeviceName()) : *InDeviceName);
	return true;
}

void FFPSRLMicrophone::Close()
{
	if (Capture)
	{
		Capture->Stop();
		Capture->Shutdown();
	}
	Capture.Reset();
	if (Encoder)
	{
		Encoder->Destroy();
	}
	Encoder.Reset();
	Unencoded.Reset();
	DeviceName.Reset();
}

void FFPSRLMicrophone::Step(float OpenThreshold, float Trim, bool bSendWhenVoice, bool bSendAlways, bool bEchoGuard, FFPSRLMicStep& Out)
{
	const double Now = FPlatformTime::Seconds();
	Out.Level = SmoothedLevel *= 0.9f;	// decays while nothing arrives
	Out.bVoice = Now - LastVoiceTime < VoiceHoldSeconds;
	if (!Capture)
	{
		return;
	}
	uint32 Available = 0;
	if (Capture->GetCaptureState(Available) != EVoiceCaptureState::Ok || Available == 0)
	{
		return;
	}
	Pcm.SetNumUninitialized(Available, EAllowShrinking::No);
	uint32 Written = 0;
	uint64 SampleCount = 0;
	const EVoiceCaptureState::Type Result = Capture->GetVoiceData(Pcm.GetData(), Pcm.Num(), Written, SampleCount);
	if ((Result != EVoiceCaptureState::Ok && Result != EVoiceCaptureState::NoData) || Written < sizeof(int16))
	{
		return;
	}
	Pcm.SetNum(Written - Written % sizeof(int16), EAllowShrinking::No);
	LastDataTime = Now;

	// 10 ms frames: activity over an adaptive noise floor, then automatic gain toward the target speech level (fast
	// down, slow up, only while voiced so silence is never pumped up), the player's trim, and a soft limiter.
	int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());
	const int32 Count = Pcm.Num() / sizeof(int16);
	const int32 FrameLength = FMath::Max(1, UVOIPStatics::GetVoiceSampleRate() / 100);
	for (int32 Start = 0; Start < Count; Start += FrameLength)
	{
		const int32 Length = FMath::Min(FrameLength, Count - Start);
		const float Level = FPSRLMicrophone::Rms(Samples + Start, Length);
		NoiseFloor = Level < NoiseFloor ? FMath::Lerp(NoiseFloor, Level, 0.2f) : FMath::Min(NoiseFloor * 1.002f + 0.00001f, 0.05f);
		// While a teammate talks (bEchoGuard) their voice can come back in through this player's speakers: the mic opens
		// only for a much louder (direct) voice, and the gain neither climbs on the echo nor goes above EchoMaxGain.
		const float Open = bEchoGuard ? FMath::Max(OpenThreshold * EchoGateFactor, NoiseFloor * EchoNoiseFactor) : FMath::Max(OpenThreshold, NoiseFloor * 3.f);
		if (Level > Open)
		{
			LastVoiceTime = Now;
			const float Desired = FMath::Clamp(TargetLevel / FMath::Max(Level, 0.0001f), 0.5f, MaxGain);
			if (!bEchoGuard || Desired < Gain)
			{
				Gain += (Desired - Gain) * (Desired < Gain ? 0.3f : 0.03f);
			}
		}
		const float TotalGain = (bEchoGuard ? FMath::Min(Gain, EchoMaxGain) : Gain) * Trim;
		for (int32 Index = Start; Index < Start + Length; ++Index)
		{
			float Value = Samples[Index] / 32768.f * TotalGain;
			const float Magnitude = FMath::Abs(Value);
			if (Magnitude > 0.8f)
			{
				Value = FMath::Sign(Value) * (0.8f + 0.2f * FMath::Tanh((Magnitude - 0.8f) / 0.2f));
			}
			Samples[Index] = static_cast<int16>(FMath::Clamp(Value, -1.f, 1.f) * 32767.f);
		}
		SmoothedLevel = FMath::Max(FPSRLMicrophone::Rms(Samples + Start, Length), SmoothedLevel * 0.9f);
	}
	Out.Level = SmoothedLevel;
	Out.bVoice = Now - LastVoiceTime < VoiceHoldSeconds;

	if (!(bSendAlways || (bSendWhenVoice && Out.bVoice)))
	{
		if (!Unencoded.IsEmpty())
		{
			Unencoded.Reset();
			Encoder->Reset();
		}
		return;
	}
	// Encode (Opus encodes whole frames; the rest waits for the next step, as in the engine's own sender).
	Unencoded.Append(Pcm);
	Compressed.SetNumUninitialized(UVOIPStatics::GetMaxCompressedVoiceDataSize(), EAllowShrinking::No);
	uint32 CompressedSize = Compressed.Num();
	const int32 Remaining = FMath::Clamp(Encoder->Encode(Unencoded.GetData(), Unencoded.Num(), Compressed.GetData(), CompressedSize), 0, Unencoded.Num());
	const int32 Consumed = Unencoded.Num() - Remaining;
	Unencoded.RemoveAt(0, Consumed, EAllowShrinking::No);
	if (CompressedSize > 0)
	{
		Out.Encoded.Append(Compressed.GetData(), CompressedSize);
		Out.SampleCount = SentSamples;	// continuous: only audio actually sent moves the stream on
		SentSamples += Consumed / sizeof(int16) / FMath::Max(1, UVOIPStatics::GetVoiceNumChannels());
		++PacketsSent;
		BytesSent += CompressedSize;
	}
}

void FFPSRLMicrophone::StartDetect(float Seconds)
{
	Probes.Reset();
	for (const FString& Name : GetDevices())
	{
		TSharedPtr<IVoiceCapture> Probe = FVoiceModule::Get().CreateVoiceCapture(Name);
		if (Probe && Probe->Start())
		{
			Probes.Add({ Name, Probe, 0.f });
		}
	}
	DetectEndTime = FPlatformTime::Seconds() + Seconds;
	UE_LOG(LogFPSRL, Log, TEXT("[Voice] microphone auto-detect: listening to %d device(s)"), Probes.Num());
}

bool FFPSRLMicrophone::StepDetect(FString& OutDevice, float& OutPeak)
{
	TArray<uint8> Buffer;
	for (FProbe& Probe : Probes)
	{
		uint32 Available = 0;
		if (Probe.Capture->GetCaptureState(Available) == EVoiceCaptureState::Ok && Available > 0)
		{
			Buffer.SetNumUninitialized(Available);
			uint32 Written = 0;
			uint64 SampleCount = 0;
			Probe.Capture->GetVoiceData(Buffer.GetData(), Buffer.Num(), Written, SampleCount);
			const int32 Count = Written / sizeof(int16);
			const int32 Window = FMath::Max(1, UVOIPStatics::GetVoiceSampleRate() / 10);	// loudest 100 ms
			for (int32 Start = 0; Start < Count; Start += Window)
			{
				Probe.Peak = FMath::Max(Probe.Peak, FPSRLMicrophone::Rms(reinterpret_cast<const int16*>(Buffer.GetData()) + Start, FMath::Min(Window, Count - Start)));
			}
		}
	}
	if (FPlatformTime::Seconds() < DetectEndTime)
	{
		return false;
	}
	OutDevice.Reset();
	OutPeak = 0.f;
	for (FProbe& Probe : Probes)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Voice] auto-detect: %s loudest %.4f"), *Probe.Name, Probe.Peak);
		if (Probe.Peak > OutPeak)
		{
			OutPeak = Probe.Peak;
			OutDevice = Probe.Name;
		}
		Probe.Capture->Stop();
		Probe.Capture->Shutdown();
	}
	Probes.Reset();
	if (OutPeak < 0.004f)	// nothing louder than room noise
	{
		OutDevice.Reset();
	}
	return true;
}

bool FFPSRLMicrophone::IsStalled() const
{
	return Capture.IsValid() && FPlatformTime::Seconds() - LastDataTime > 3.0;
}
