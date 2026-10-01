// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLCombatFeedback.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

void UFPSRLPunchShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(Duration);
}

void UFPSRLPunchShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	Elapsed = 0.f;
}

void UFPSRLPunchShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	Elapsed += Params.DeltaTime;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(Duration, 0.01f), 0.f, 1.f);
	// A fast kick down that springs back.
	OutResult.Rotation.Pitch = -PitchAmplitude * FMath::Sin(PI * Alpha) * (1.f - Alpha) * Params.GetTotalScale();
}

bool UFPSRLPunchShakePattern::IsFinishedImpl() const
{
	return Elapsed >= Duration;
}

UFPSRLMeleePunchShake::UFPSRLMeleePunchShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetRootShakePattern(CreateDefaultSubobject<UFPSRLPunchShakePattern>(TEXT("Punch")));
}

namespace FPSRLCombatFeedback
{
	constexpr int32 SampleRate = 44100;

	/** One cached clip per feedback kind (16-bit mono). */
	const TArray<int16>& GetClip(EFPSRLHitFeedback Kind)
	{
		static TMap<EFPSRLHitFeedback, TArray<int16>> Clips;
		if (const TArray<int16>* Found = Clips.Find(Kind))
		{
			return *Found;
		}
		TArray<int16>& Clip = Clips.Add(Kind);
		FRandomStream Noise(static_cast<int32>(Kind) + 11);
		auto Tone = [&Clip, &Noise](float Seconds, float StartHz, float EndHz, float NoiseMix, float Volume, float Decay)
		{
			const int32 Count = FMath::RoundToInt(Seconds * SampleRate);
			const int32 Offset = Clip.Num();
			Clip.AddZeroed(Count);
			float Phase = 0.f;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float T = static_cast<float>(Index) / Count;
				Phase += 2.f * PI * FMath::Lerp(StartHz, EndHz, T) / SampleRate;
				const float Sample = (1.f - NoiseMix) * FMath::Sin(Phase) + NoiseMix * Noise.FRandRange(-1.f, 1.f);
				const float Envelope = FMath::Min(1.f, Index / 80.f) * FMath::Exp(-Decay * T);	// quick attack, decay
				Clip[Offset + Index] = static_cast<int16>(FMath::Clamp(Sample * Envelope * Volume, -1.f, 1.f) * 32000.f);
			}
		};
		switch (Kind)
		{
		case EFPSRLHitFeedback::Critical:	Tone(0.06f, 3200.f, 2600.f, 0.1f, 0.5f, 5.f); Tone(0.05f, 4200.f, 3800.f, 0.1f, 0.4f, 6.f); break;
		case EFPSRLHitFeedback::Kill:		Tone(0.05f, 1800.f, 1600.f, 0.1f, 0.5f, 4.f); Tone(0.09f, 900.f, 500.f, 0.2f, 0.6f, 4.f); break;
		case EFPSRLHitFeedback::MeleeHit:	Tone(0.10f, 160.f, 70.f, 0.45f, 0.9f, 4.f); break;
		case EFPSRLHitFeedback::MeleeKill:	Tone(0.12f, 140.f, 50.f, 0.5f, 1.f, 3.f); Tone(0.08f, 700.f, 400.f, 0.2f, 0.4f, 4.f); break;
		case EFPSRLHitFeedback::MeleeMiss:	Tone(0.22f, 700.f, 220.f, 0.75f, 0.85f, 2.5f); break;	// a whoosh (louder: the v0.1.21 one was inaudible)
		default:							Tone(0.04f, 2600.f, 2200.f, 0.1f, 0.45f, 6.f); break;	// a short tick
		}
		return Clip;
	}

	void PlaySound(const UObject* WorldContext, EFPSRLHitFeedback Kind)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		const TArray<int16>& Clip = GetClip(Kind);
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
		Wave->SetSampleRate(SampleRate);
		Wave->NumChannels = 1;
		Wave->Duration = static_cast<float>(Clip.Num()) / SampleRate;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Clip.GetData()), Clip.Num() * sizeof(int16));
		UGameplayStatics::PlaySound2D(World, Wave);
	}
}
