// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLUserSettings.h"
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
