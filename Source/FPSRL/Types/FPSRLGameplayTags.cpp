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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Dash,	"Ability.Dash",		"Dash ability.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Melee,	"Ability.Melee",	"Melee attack ability.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Reload,	"Ability.Reload",	"Weapon reload ability.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Hit,	"Event.Hit",	"Sent to the attacker's ASC when their damage lands.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kill,	"Event.Kill",	"Sent to the attacker's ASC when their damage kills.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage,		"SetByCaller.Damage",		"Base damage amount for GE_Damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Healing,		"SetByCaller.Healing",		"Heal amount for the heal effect.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Corruption,	"SetByCaller.Corruption",	"Run corruption level for enemy scaling effects.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cooldown_Dash,	"Cooldown.Dash",	"Dash is on cooldown.");
}
