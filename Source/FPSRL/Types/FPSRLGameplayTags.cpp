// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/FPSRLGameplayTags.h"

namespace FPSRLGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Rifle,	"Weapon.Rifle",		"Lobby-selectable rifle.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Pistol,	"Weapon.Pistol",	"Lobby-selectable pistol.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cannon,	"Weapon.Cannon",	"Lobby-selectable cannon (grenade launcher).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Bullet,		"Damage.Bullet",	"Damage from a hitscan or bullet projectile.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Explosive,	"Damage.Explosive",	"Damage from an explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Melee,		"Damage.Melee",		"Damage from a melee attack.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Dead,			"Status.Dead",			"Actor is dead; ignores further damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Invulnerable,	"Status.Invulnerable",	"Actor ignores incoming damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Dashing,		"Status.Dashing",		"Actor is mid-dash.");
}
