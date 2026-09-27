// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLBoonSettings.h"
#include "Types/FPSRLGameplayTags.h"

UFPSRLBoonSettings::UFPSRLBoonSettings()
{
	DefaultSecondaryItem = FPSRLGameplayTags::Secondary_Melee;

	RelicRarityWeights.Add(ERelicRarity::Normal, 50.f);
	RelicRarityWeights.Add(ERelicRarity::Uncommon, 25.f);
	RelicRarityWeights.Add(ERelicRarity::Rare, 15.f);
	RelicRarityWeights.Add(ERelicRarity::Epic, 8.f);
	RelicRarityWeights.Add(ERelicRarity::Legendary, 2.f);
}
