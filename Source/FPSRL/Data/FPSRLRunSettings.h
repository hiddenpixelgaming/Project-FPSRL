// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRunSettings.generated.h"

class UFPSRLRunDefinition;

/**
 * Run structure tuning. Edit in Project Settings > Game > FPSRL Run (saved to Config/DefaultGame.ini).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Run"))
class FPSRL_API UFPSRLRunSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UFPSRLRunSettings& Get() { return *GetDefault<UFPSRLRunSettings>(); }

	/** The expedition started by the Lobby's Start button. Unset = the Lobby's MatchMap is used as a one-Depth run. */
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	TSoftObjectPtr<UFPSRLRunDefinition> RunDefinition;

	/** Where the party returns when a run ends. */
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	TSoftObjectPtr<UWorld> LobbyMap;

	/** What each Traversal reward spawns at the traversal's reward point (Blessing Altar, Upgrade Altar, future ones). */
	UPROPERTY(Config, EditAnywhere, Category = "Rooms")
	TMap<EFPSRLTraversalReward, TSoftClassPtr<AActor>> TraversalRewardClasses;

	/** A traversal's reward (Blessing / Upgrade Altar) is given at the end of the encounter room before it instead, so
	 *  nobody can walk past it: spawned when that room loads (locked until it is cleared), here relative to its exit (the next room's origin,
	 *  facing on). Default: beside the exit door, at the side of the exit passage. */
	UPROPERTY(Config, EditAnywhere, Category = "Rooms")
	FVector EncounterRewardOffsetFromExit = FVector(-280.f, -230.f, 60.f);

	/** The Final Level Boss room's altars (user: all three, fixed, no random roll), unlocked when the boss is defeated. */
	UPROPERTY(Config, EditAnywhere, Category = "Rooms")
	TArray<EFPSRLTraversalReward> FinalBossRewards = { EFPSRLTraversalReward::BlessingAltar, EFPSRLTraversalReward::UpgradeAltar, EFPSRLTraversalReward::HealingAltar };

	/** Where each of them stands, relative to the boss room's exit like EncounterRewardOffsetFromExit: by the exit wall
	 *  (two left of the door, one right), in view from the entrance and clear of the boss in the middle of the room. */
	UPROPERTY(Config, EditAnywhere, Category = "Rooms")
	TArray<FVector> FinalBossRewardOffsets = { FVector(-150.f, -620.f, 60.f), FVector(-150.f, -380.f, 60.f), FVector(-150.f, 380.f, 60.f) };

	/** Seconds between an occupancy check and the next when unloading rooms every player has left behind. */
	UPROPERTY(Config, EditAnywhere, Category = "Rooms", meta = (ClampMin = "0.1"))
	float RoomUnloadCheckInterval = 1.f;

	/** A player who falls out of the level: back to safety with damage (the real game) or killed (playtesting). */
	UPROPERTY(Config, EditAnywhere, Category = "Falling")
	EFPSRLFallResponse FallResponse = EFPSRLFallResponse::ReturnToSafety;

	/** ReturnToSafety: share of max health lost per fall (can down the player). */
	UPROPERTY(Config, EditAnywhere, Category = "Falling", meta = (ClampMin = "0", ClampMax = "1"))
	float FallDamageFraction = 0.05f;	// user: 20% was too punishing

	/** How far below the lowest room the fall volume's top sits. */
	UPROPERTY(Config, EditAnywhere, Category = "Falling", meta = (ClampMin = "100"))
	float FallVolumeDepth = 1500.f;

	/** How far the fall volume reaches past the Depth's rooms on every side. */
	UPROPERTY(Config, EditAnywhere, Category = "Falling", meta = (ClampMin = "0"))
	float FallVolumeMargin = 10000.f;

	/** Seconds between the Depth completing and the portal accepting players (activation VFX window). */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0"))
	float PortalActivationDelay = 1.f;

	/** Share of living players choosing Continue that starts the countdown (0.5 = half or more). */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0.01", ClampMax = "1"))
	float PortalVoteThreshold = 0.5f;

	/**
	 * Once the threshold is met, the rest of the living party has this long before everyone is moved together.
	 * 0 = no countdown: wait until every living player chooses Continue.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Exit Portal", meta = (ClampMin = "0"))
	float PortalCountdownSeconds = 35.f;

	/**
	 * Catch-up (players move at their own pace inside a Depth): when an encounter room starts while teammates are still
	 * in earlier rooms, each of them who is up gets their own optional offer to be moved to that room's entrance. Nobody
	 * is ever moved without accepting; ignoring or declining changes nothing; combat never waits.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	bool bCatchUpEnabled = true;

	/** Which encounter rooms offer catch-up when they start. */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	bool bCatchUpToCombatRooms = true;

	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	bool bCatchUpToMiniboss = true;

	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	bool bCatchUpToFinalBoss = true;

	/** Seconds after a room starts before players still behind get the offer (teammates a few steps behind arrive on
	 *  their own and never see it). One offer per player per started room. */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up", meta = (ClampMin = "0"))
	float CatchUpOfferDelay = 0.5f;	// playtest v0.1.32: 3 s felt too slow

	/** Candidate arrival spots, in the room's own space (every room's entrance is its origin, facing +X; Z = capsule
	 *  centre above the floor). The first safe one is used: on the navmesh, clear of geometry, players, enemies and
	 *  hazards. Before the combat trigger, so the player walks into the fight. */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	TArray<FVector> CatchUpSpawnOffsets = {
		FVector(300.f, 0.f, 100.f), FVector(300.f, 160.f, 100.f), FVector(300.f, -160.f, 100.f),
		FVector(180.f, 0.f, 100.f), FVector(180.f, 230.f, 100.f), FVector(180.f, -230.f, 100.f),
		FVector(450.f, 260.f, 100.f), FVector(450.f, -260.f, 100.f) };

	/** A catch-up spot is unsafe with a living enemy closer than this. */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up", meta = (ClampMin = "0"))
	float CatchUpEnemyClearance = 500.f;

	/** Tell the others when a player is the first to move on into a new room. */
	UPROPERTY(Config, EditAnywhere, Category = "Catch-Up")
	bool bAnnouncePlayerAdvance = true;

	/** Move speed while downed, as a fraction of normal (0.2 = 20%). */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0", ClampMax = "1"))
	float DownedMoveSpeedMultiplier = 0.2f;

	/** Seconds a teammate must stay beside a downed player to revive them. */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0"))
	float ReviveSeconds = 3.f;

	/** Share of max health a revived player gets back (0.15 = 15%). */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ReviveHealthFraction = 0.15f;

	/** Damage a reviver can take during one revive before it is interrupted (total since the revive started; 0 = damage
	 *  never interrupts). Playtest v0.1.32: enemies should be able to break a revive. */
	UPROPERTY(Config, EditAnywhere, Category = "Downed", meta = (ClampMin = "0"))
	float ReviveInterruptDamage = 40.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
