// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLBoonDefinition.h"
#include "Data/FPSRLAspectDefinition.h"

bool UFPSRLBoonDefinition::AllowsChannel(EFPSRLBoonChannel Channel) const
{
	return (AllowedChannels.IsEmpty() || AllowedChannels.Contains(Channel)) && (!Aspect || Aspect->AllowsChannel(Channel));
}

FPrimaryAssetId UFPSRLBoonDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Boon"), GetFName());
}
