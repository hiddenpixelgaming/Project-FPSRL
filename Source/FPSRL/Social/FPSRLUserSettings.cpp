// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLUserSettings.h"
#include "Containers/Ticker.h"
#include "FPSRL.h"

FFPSRLUserSettingsChanged UFPSRLUserSettings::OnChanged;

void UFPSRLUserSettings::SetFilterProfanity(bool bEnabled)
{
	UFPSRLUserSettings* Settings = GetMutableDefault<UFPSRLUserSettings>();
	if (Settings->bFilterProfanity == bEnabled)
	{
		return;
	}
	Settings->bFilterProfanity = bEnabled;
	Settings->SaveConfig();
	UE_LOG(LogFPSRL, Log, TEXT("[Settings] Profanity filter %s"), bEnabled ? TEXT("on") : TEXT("off"));
	OnChanged.Broadcast();
}

namespace FPSRLUserSettingsPrivate
{
	static FTSTicker::FDelegateHandle PendingSave;
	static FString PendingWhat;

	/** Writes the settings file once changes settle (a slider sends dozens of changes a second while dragged). */
	static void ScheduleSave(const TCHAR* What)
	{
		if (!PendingWhat.Contains(What))
		{
			PendingWhat += PendingWhat.IsEmpty() ? FString(What) : FString::Printf(TEXT(", %s"), What);
		}
		FTSTicker::RemoveTicker(PendingSave);
		PendingSave = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			GetMutableDefault<UFPSRLUserSettings>()->SaveConfig();
			UE_LOG(LogFPSRL, Log, TEXT("[Settings] saved (%s changed)"), *PendingWhat);
			PendingWhat.Reset();
			PendingSave.Reset();
			return false;
		}), 0.5f);
	}

	template <typename T>
	static void Set(T UFPSRLUserSettings::* Member, const T& Value, const TCHAR* What)
	{
		UFPSRLUserSettings* Settings = GetMutableDefault<UFPSRLUserSettings>();
		if (Settings->*Member == Value)
		{
			return;
		}
		Settings->*Member = Value;
		ScheduleSave(What);
		UE_LOG(LogFPSRL, Verbose, TEXT("[Settings] %s changed"), What);
		UFPSRLUserSettings::OnChanged.Broadcast();
	}
}

void UFPSRLUserSettings::SetVoiceChatEnabled(bool bEnabled)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::bVoiceChatEnabled, bEnabled, TEXT("Voice chat"));
}

void UFPSRLUserSettings::SetVoiceChatVolume(float Volume)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::VoiceChatVolume, FMath::Clamp(Volume, 0.f, 2.f), TEXT("Voice chat volume"));
}

void UFPSRLUserSettings::SetMicrophoneVolume(float Volume)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::MicrophoneVolume, FMath::Clamp(Volume, 0.f, 2.f), TEXT("Microphone volume"));
}

void UFPSRLUserSettings::SetVoiceInputMode(EFPSRLVoiceInputMode Mode)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::VoiceInputMode, Mode, TEXT("Voice input mode"));
}

void UFPSRLUserSettings::SetPushToTalkKey(const FKey& Key)
{
	if (Key.IsValid() && !Key.IsGamepadKey())	// keyboard and mouse only (user)
	{
		FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::PushToTalkKey, Key, TEXT("Push-to-Talk key"));
	}
}

void UFPSRLUserSettings::SetVoiceOutputDevice(const FString& DeviceName)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::VoiceOutputDevice, DeviceName, TEXT("Voice output device"));
}

void UFPSRLUserSettings::SetOpenMicSensitivity(float Sensitivity)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::OpenMicSensitivity, FMath::Clamp(Sensitivity, 0.f, 1.f), TEXT("Open mic sensitivity"));
}

void UFPSRLUserSettings::SetMicrophoneDevice(const FString& DeviceName)
{
	FPSRLUserSettingsPrivate::Set(&UFPSRLUserSettings::MicrophoneDevice, DeviceName, TEXT("Microphone"));
}

void UFPSRLUserSettings::FlushPendingSave()
{
	if (FPSRLUserSettingsPrivate::PendingSave.IsValid())
	{
		FTSTicker::RemoveTicker(FPSRLUserSettingsPrivate::PendingSave);
		FPSRLUserSettingsPrivate::PendingSave.Reset();
		GetMutableDefault<UFPSRLUserSettings>()->SaveConfig();
		UE_LOG(LogFPSRL, Log, TEXT("[Settings] saved (%s changed)"), *FPSRLUserSettingsPrivate::PendingWhat);
		FPSRLUserSettingsPrivate::PendingWhat.Reset();
	}
}
