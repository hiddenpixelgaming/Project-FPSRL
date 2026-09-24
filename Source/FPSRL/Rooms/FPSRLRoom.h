// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLRoom.generated.h"

class AController;
class AFPSRLDoor;
class AFPSRLTriggerVolume;
class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFPSRLRoomEvent);

/**
 * One handcrafted room of a Depth and its encounter (replaces BP_ArenaManager). BP_Room derives from this.
 *
 * Combat flow (server): the first player through CombatTrigger starts the encounter exactly once -> the exit door
 * locks -> the room tracks its LIVING enemies -> the last one dies -> RoomCompleted: the exit unlocks, OnRoomCompleted
 * fires, and the Depth is told (AFPSRLGameState). Enemies that died before the encounter began are never counted, so
 * an early kill can't leave the room unclearable. No CombatTrigger = the encounter starts when play begins.
 *
 * Enemies: the explicit Enemies list, or (if empty) every EnemyClass actor inside RoomBounds when combat starts.
 * Rooms with bRequiredForDepth count toward the Depth's exit portal; optional rooms (rewards, altars) don't.
 * Event-driven, no Tick.
 */
UCLASS()
class FPSRL_API AFPSRLRoom : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLRoom();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	ERoomType RoomType = ERoomType::Combat;

	/** Counts toward the Depth's completion (the exit portal waits for it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	bool bRequiredForDepth = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room")
	TObjectPtr<AFPSRLTriggerVolume> CombatTrigger;

	/** Locked while the encounter runs, unlocked when the room is cleared. Optional. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room")
	TObjectPtr<AFPSRLDoor> ExitDoor;

	/** This room's enemies. Leave empty to use every EnemyClass actor inside RoomBounds. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room|Enemies")
	TArray<TObjectPtr<AActor>> Enemies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Enemies")
	TSubclassOf<AActor> EnemyClass;

	UPROPERTY(ReplicatedUsing = OnRep_RoomState, BlueprintReadOnly, Category = "Room")
	bool bCombatStarted = false;

	UPROPERTY(ReplicatedUsing = OnRep_RoomState, BlueprintReadOnly, Category = "Room")
	bool bRoomComplete = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room")
	int32 RemainingEnemies = 0;

	UPROPERTY(BlueprintAssignable, Category = "Room")
	FFPSRLRoomEvent OnCombatStarted;

	/** The encounter is over (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FFPSRLRoomEvent OnRoomCompleted;

	UFUNCTION(BlueprintPure, Category = "Room")
	bool IsRoomComplete() const { return bRoomComplete; }

	/** Server: start the encounter now (normally the combat trigger does this). Ignored after the first time. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Room")
	void StartCombat();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	/** Volume enemies are gathered from when the Enemies list is empty. Editor-only visual, no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TObjectPtr<UBoxComponent> RoomBounds;

private:
	UFUNCTION()
	void HandleCombatTriggered(AActor* TriggeringActor);

	UFUNCTION()
	void HandleEnemyDeath(AController* Killer, AActor* Causer);

	UFUNCTION()
	void OnRep_RoomState();

	TArray<AActor*> GatherEnemies() const;
	void CompleteRoom();

	bool bBroadcastCombatStarted = false;
	bool bBroadcastCompleted = false;
};
