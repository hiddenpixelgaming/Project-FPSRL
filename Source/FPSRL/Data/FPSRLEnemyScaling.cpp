// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLEnemyScaling.h"

UFPSRLEnemyScalingSettings::UFPSRLEnemyScalingSettings()
{
	// Abyssus-verified player-count health curve and encounter caps. Damage by player count: project balancing (flat).
	EnemyScaling.Health = { 1.f, 2.f, 3.25f, 4.75f };
	EnemyScaling.Damage = { 1.f, 1.f, 1.f, 1.f };
	EnemyScaling.MaxEnemies = { 12, 15, 17, 20 };

	// Bosses: separate profile (project balancing; starts on the same curve, one boss encounter at a time).
	BossScaling.Health = { 1.f, 2.f, 3.25f, 4.75f };
	BossScaling.Damage = { 1.f, 1.f, 1.f, 1.f };
	BossScaling.MaxEnemies = { 1, 1, 1, 1 };
}

const UFPSRLEnemyDefinition* UFPSRLEnemyScalingSettings::FindDefinition(const UClass* EnemyClass) const
{
	const UFPSRLEnemyDefinition* Best = nullptr;
	int32 BestDepth = MAX_int32;
	for (const TSoftObjectPtr<UFPSRLEnemyDefinition>& Entry : EnemyDefinitions)
	{
		const UFPSRLEnemyDefinition* Definition = Entry.LoadSynchronous();
		const UClass* DefinedClass = Definition ? Definition->EnemyClass.LoadSynchronous() : nullptr;
		if (!DefinedClass || !EnemyClass || !EnemyClass->IsChildOf(DefinedClass))
		{
			continue;
		}
		int32 Depth = 0;	// how far up the hierarchy: the closest match wins
		for (const UClass* Class = EnemyClass; Class && Class != DefinedClass; Class = Class->GetSuperClass())
		{
			++Depth;
		}
		if (Depth < BestDepth)
		{
			BestDepth = Depth;
			Best = Definition;
		}
	}
	return Best;
}
