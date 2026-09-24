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
