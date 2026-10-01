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

/** What a room inside a Depth is for. Combat, Miniboss and Boss (the Final Level Boss arena) rooms are the encounters a Depth
 *  requires; Traversal spaces sit between them and never count as combat rooms. */
UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Entry,
	Combat,
	Miniboss,
	Reward,
	Boon,
	Merchant,
	Upgrade,
	Boss,			// the Final Level Boss arena
	Traversal,		// corridor between encounters: streaming, movement, optional altar (see EFPSRLTraversalReward)
	Preparation		// safe room before the Final Level Boss (altars, future Merchant)
};

/** What happens to a player who falls out of the level (AFPSRLFallVolume). */
UENUM(BlueprintType)
enum class EFPSRLFallResponse : uint8
{
	ReturnToSafety,		// back on the floor of the room they fell from, minus a share of max health (the real game)
	InstantDeath		// killed outright (playtesting)
};

/** What a Traversal space holds, rolled per traversal from the Depth's data. Future = reserved for later interactables. */
UENUM(BlueprintType)
enum class EFPSRLTraversalReward : uint8
{
	None,
	BlessingAltar,
	UpgradeAltar,
	Future
};

/** What kind of encounter a room runs. Miniboss and Final Level Boss are data (UFPSRLEncounterDefinition), not enemies. */
UENUM(BlueprintType)
enum class EFPSRLEncounterKind : uint8
{
	Normal,
	Miniboss,
	FinalLevelBoss
};

/** A Depth's progress (AFPSRLGameState). */
UENUM(BlueprintType)
enum class EFPSRLDepthState : uint8
{
	NotStarted,
	Loading,		// first rooms streaming in
	Active,
	Completing,		// every required encounter done; the exit portal is activating
	Completed
};

/** One room of a Depth, as the room-by-room streaming sees it (UFPSRLDepthLayoutComponent). */
UENUM(BlueprintType)
enum class EFPSRLRoomState : uint8
{
	Unloaded,
	Loading,
	Active,			// loaded, encounter not started (or no encounter)
	Combat,
	Completed,
	Unloading
};

/** The Final Level Boss encounter of the current Depth (AFPSRLGameState). */
UENUM(BlueprintType)
enum class EFPSRLBossState : uint8
{
	NotStarted,
	Loading,
	Active,
	Defeated
};

/** Pacing role of a Depth within its level (Normal -> Normal + Miniboss -> Normal -> Preparation + Final Level Boss). */
UENUM(BlueprintType)
enum class EDepthType : uint8
{
	Normal,
	Miniboss,
	Preparation,
	FinalLevelBoss
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
	FinalLevelBoss,
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

/**
 * The three combat channels a player's Blessings (boons) attach to. Each channel gets its own Aspect (boon family)
 * and progresses independently. The equipped item (a weapon, the melee, a future ability) is data supplied to the
 * channel, so new weapons and abilities plug in without a new boon system.
 */
UENUM(BlueprintType)
enum class EFPSRLBoonChannel : uint8
{
	Primary,	// lobby weapon (Pistol / Rifle / Cannon)
	Secondary,	// built-in melee today; melee or artifact weapons later
	Ability,	// framework only until a real ability exists
	MAX UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EFPSRLBoonChannel, EFPSRLBoonChannel::MAX);

/** A Blessing's kind. Which kind a slot position offers is data (BoonSettings.PositionTypes; default 3rd = Minor,
 *  6th = Major, every other position Normal), so Minor and Major Blessings only ever appear at their positions. */
UENUM(BlueprintType)
enum class EFPSRLBoonType : uint8
{
	Normal,
	Minor,	// offered at the slot positions set as Minor (default: the 3rd Blessing)
	Major	// offered at the slot positions set as Major (default: the 6th Blessing)
};

/** What kind of attack an equipped item makes, for Blessing compatibility (a Blessing lists the sources it supports;
 *  none listed = Universal). Items map to a source in Project Settings > FPSRL Blessings (ItemSources). */
UENUM(BlueprintType)
enum class EFPSRLItemSource : uint8
{
	Ranged,
	Melee,
	Ability
};

/** Who a Blessing affects. Only Self is implemented; the others are reserved for explicitly team-wide designs. */
UENUM(BlueprintType)
enum class EFPSRLBoonScope : uint8
{
	Self,
	Team,
	Target,
	Area
};

/** What the attacking player is told about one of their hits (HUD marker, sound, view punch). */
UENUM(BlueprintType)
enum class EFPSRLHitFeedback : uint8
{
	Hit,
	Critical,
	Kill,
	MeleeHit,
	MeleeKill,
	MeleeMiss		// a swing that hit nothing
};
