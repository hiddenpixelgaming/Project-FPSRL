// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLAspectDefinition.h"

const FFPSRLGrantSet* UFPSRLAspectDefinition::GetGrants(EFPSRLBoonChannel Channel, int32 UpgradeLevel) const
{
	const FFPSRLAspectChannelGrants* Entry = ChannelGrants.FindByPredicate(
		[Channel](const FFPSRLAspectChannelGrants& Candidate) { return Candidate.Channel == Channel; });
	if (!Entry)
	{
		return nullptr;
	}
	return (UpgradeLevel > 0 && !FPSRLGrants::IsEmpty(Entry->UpgradedGrants)) ? &Entry->UpgradedGrants : &Entry->Grants;
}

FPrimaryAssetId UFPSRLAspectDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Aspect"), GetFName());
}
