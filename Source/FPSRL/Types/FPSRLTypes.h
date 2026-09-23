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

/**
 * Role of a room within the 24-room run sequence.
 * Replaces encoding the type as which BP_RoomInfo_* subclass an asset uses.
 */
UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Spawn,
	Walk,
	Arena,
	SubBoss,
	FinalBoss,
	EndRun
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
