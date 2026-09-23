// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLPlayerControllerMenu.h"

AFPSRLPlayerControllerMenu::AFPSRLPlayerControllerMenu()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AFPSRLPlayerControllerMenu::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		SetShowMouseCursor(true);
	}
}

void AFPSRLPlayerControllerMenu::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}

	Super::EndPlay(EndPlayReason);
}
