// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLRunDefinition.h"

int32 UFPSRLRunDefinition::GetNumDepths() const
{
	int32 Count = 0;
	for (const FFPSRLAreaEntry& Area : Areas)
	{
		Count += Area.Depths.Num();
	}
	return Count;
}

const UFPSRLDepthDefinition* UFPSRLRunDefinition::GetDepth(int32 FlatIndex, int32* OutAreaIndex, int32* OutDepthInArea) const
{
	if (FlatIndex < 0)
	{
		return nullptr;
	}
	for (int32 AreaIndex = 0; AreaIndex < Areas.Num(); ++AreaIndex)
	{
		const TArray<TObjectPtr<UFPSRLDepthDefinition>>& Depths = Areas[AreaIndex].Depths;
		if (FlatIndex < Depths.Num())
		{
			if (OutAreaIndex)
			{
				*OutAreaIndex = AreaIndex;
			}
			if (OutDepthInArea)
			{
				*OutDepthInArea = FlatIndex;
			}
			return Depths[FlatIndex];
		}
		FlatIndex -= Depths.Num();
	}
	return nullptr;
}

EFPSRLTraversalReward FFPSRLTraversalRewardOdds::Roll() const
{
	float Total = 0.f;
	for (const TPair<EFPSRLTraversalReward, float>& Entry : Weights)
	{
		Total += FMath::Max(0.f, Entry.Value);
	}
	if (Total <= 0.f)
	{
		return EFPSRLTraversalReward::None;
	}
	float Roll = FMath::FRandRange(0.f, Total);
	for (const TPair<EFPSRLTraversalReward, float>& Entry : Weights)
	{
		Roll -= FMath::Max(0.f, Entry.Value);
		if (Roll <= 0.f && Entry.Value > 0.f)
		{
			return Entry.Key;
		}
	}
	return EFPSRLTraversalReward::None;
}
