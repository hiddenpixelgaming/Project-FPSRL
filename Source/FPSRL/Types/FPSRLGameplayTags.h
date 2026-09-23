// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native Gameplay Tags. Registered at module startup, so no DefaultGameplayTags.ini entry is needed,
 * and C++ references them as FPSRLGameplayTags::Weapon_Rifle with compile-time checking.
 * They also appear in the editor's tag picker for Blueprints and Data Assets.
 *
 * Add a tag: declare it here, define it in FPSRLGameplayTags.cpp.
 */
namespace FPSRLGameplayTags
{
	// Lobby-selectable weapons. Replaces the SelectedWeaponIndex byte (0 Rifle / 1 Pistol / 2 Cannon).
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Rifle);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Pistol);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weapon_Cannon);

	// Damage sources, for boons/relics that react to how damage was dealt (e.g. Explosive Rounds).
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Bullet);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Explosive);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Melee);

	// Actor statuses, checked by gameplay code (damage intake, input, AI targeting).
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Dead);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Invulnerable);	// e.g. boss phase transition window
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Dashing);

	// --- Gameplay Ability System ---
	// GameplayCue.* tags are added alongside their first cue asset, since each cue is found by its tag.

	// Identify granted abilities (activate by tag, block/cancel by tag). Dash/Melee/Reload move to GAS in Step F.
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Dash);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Melee);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Reload);

	// Gameplay events sent to an ASC; passive boon/relic abilities listen for these (e.g. Explosive Rounds on Event.Hit).
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Hit);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kill);

	// Runtime magnitudes passed into Gameplay Effects by code.
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage);
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Corruption);

	// Granted by cooldown Gameplay Effects; an ability can't activate while its cooldown tag is present.
	FPSRL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cooldown_Dash);
}
