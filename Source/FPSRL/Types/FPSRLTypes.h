// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FPSRLTypes.generated.h"

/**
 * Shared enums used across multiple FPSRL systems.
 *
 * Rule of thumb for enum vs Gameplay Tag:
 *  - Enum: a closed set the code branches on, where order or exhaustiveness matters (door states, rarity tiers).
 *  - Gameplay Tag (see FPSRLGameplayTags.h): an open set designers extend without code changes (weapons, damage types, statuses).
 *
 * Structs that belong to one system (e.g. a DataTable row) live next to that system, not here.
 */

/**
 * Replicated state of a room door. Server sets it; clients react in OnRep.
 * Numeric values intentionally match the old BP_Door byte convention so existing data maps 1:1 on reparent.
 * The old values 5 (Unlocked, transient) and 6 (LockedOneWay, unused) are retired.
 */
UENUM(BlueprintType)
enum class EDoorState : uint8
{
	Closed	= 0,
	Opening	= 1,
	Open	= 2,	// Only walkable state.
	Closing	= 3,
	Locked	= 4		// Closed and refuses Open() until Unlock().
};

/** What a room inside a Depth is for. Combat, Elite and Boss rooms are the ones a Depth can require. */
UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Entry,
	Combat,
	Elite,
	Reward,
	Boon,
	Merchant,
	Upgrade,
	Boss
};

/** Pacing role of a Depth within its Area (NORMAL -> NORMAL + ELITE -> PREPARATION -> AREA BOSS, then the final area). */
UENUM(BlueprintType)
enum class EDepthType : uint8
{
	Normal,
	Elite,
	Preparation,
	AreaBoss,
	FinalArea,
	FinalBoss
};

/** Exit portal state machine. Only Active accepts players. */
UENUM(BlueprintType)
enum class EPortalState : uint8
{
	Hidden,		// Depth not complete; portal invisible and inert
	Locked,		// Depth not complete; portal visible but inert ("clear remaining encounters")
	Activating,	// Depth just completed; short activation delay (VFX/SFX)
	Active,		// players may enter
	Used,		// travel started
	Disabled	// never activates (e.g. a Depth without an exit)
};

/** Where an exit portal leads, derived from the run definition. */
UENUM(BlueprintType)
enum class EPortalDestination : uint8
{
	NextDepth,
	NextArea,
	FinalBoss,
	RunComplete
};

/** Relic rarity tier, ordered lowest to highest. Drop weights live in the rarity DataTable, not here. */
UENUM(BlueprintType)
enum class ERelicRarity : uint8
{
	Normal,
	Uncommon,
	Rare,
	Epic,
	Legendary
};
