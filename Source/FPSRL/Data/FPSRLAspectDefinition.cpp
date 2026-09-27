// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLAspectDefinition.h"

const FFPSRLGrantSet* UFPSRLAspectDefinition::GetGrants(EFPSRLBoonChannel Channel) const
{
	const FFPSRLAspectChannelGrants* Entry = ChannelGrants.FindByPredicate(
		[Channel](const FFPSRLAspectChannelGrants& Candidate) { return Candidate.Channel == Channel; });
	return Entry ? &Entry->Grants : nullptr;
}

FPrimaryAssetId UFPSRLAspectDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Aspect"), GetFName());
}
