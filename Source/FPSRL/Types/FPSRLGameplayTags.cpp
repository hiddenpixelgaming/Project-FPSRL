// Fill out your copyright notice in the Description page of Project Settings.

#include "Types/FPSRLGameplayTags.h"

namespace FPSRLGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element,			"Element",			"Root of the six elements.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Air,		"Element.Air",		"Air: move speed, attack speed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Fire,	"Element.Fire",		"Fire: AoE, damage over time, spread.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Water,	"Element.Water",	"Water: crowd control, heal-on-hit chance.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Earth,	"Element.Earth",	"Earth: crit chance, crit damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Light,	"Element.Light",	"Light: elemental damage, proc upgrades.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Element_Dark,	"Element.Dark",		"Dark: physical damage, defense reduction, penetration.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Aspect,			"Aspect",			"Root for aspect identity tags (Aspect.Gunslinger, ...).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Boon_Category,	"Boon.Category",	"Root for boon categories.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Build,			"Build",			"Root for build-direction tags (Build.RapidFire, ...).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect_Temporary_Run,	"Effect.Temporary.Run",		"Granted by a temporary run system; removed at run end.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Effect_Permanent_Talent,	"Effect.Permanent.Talent",	"Permanent progression; never removed by run cleanup.");

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
