// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLGameModeMenu.h"
#include "Engine/Engine.h"
#include "FPSRL.h"

void AFPSRLGameModeMenu::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_ListenServer && GEngine)
	{
		UE_LOG(LogFPSRL, Warning, TEXT("Menu was started as a listen server; shutting down its net driver so hosting can claim the Steam port."));
		GEngine->ShutdownWorldNetDriver(World);
	}
}
