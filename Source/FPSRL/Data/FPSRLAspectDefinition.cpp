// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLAspectDefinition.h"

bool UFPSRLAspectDefinition::IsCompatibleWithWeapon(const FGameplayTag& WeaponTag) const
{
	if (AssociatedWeapon.IsValid() && !WeaponTag.MatchesTagExact(AssociatedWeapon))
	{
		return false;
	}
	return RequiredWeaponTags.IsEmpty() || RequiredWeaponTags.HasTagExact(WeaponTag);
}

FPrimaryAssetId UFPSRLAspectDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Aspect"), GetFName());
}
