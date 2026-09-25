// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/FPSRLTypes.h"
#include "FPSRLDoor.generated.h"

class UArrowComponent;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class ACharacter;
class APlayerState;

/**
 * Room door with an optional per-player one-way mode. BP_Door derives from this.
 *
 * Orientation: the door's local +X axis points INTO the room (shown by the InsideDirection arrow).
 *
 * Normal door: Open = walkable for everyone; Closed/Locked = solid for everyone (the ArenaManager locks the exit
 * during combat and unlocks it on clear).
 *
 * One-way entrance (bIsOneWayEntrance): the Blocker wall is always solid, but each player gets through it exactly
 * once, inward:
 *  1. Entering the DoorwayZone (which straddles the wall) grants that player passage through the Blocker,
 *     unless they have already crossed this door.
 *  2. Leaving the DoorwayZone revokes passage. Which side they leave on decides the outcome: leaving on the
 *     inside (+X) marks them as crossed, so the wall is solid for them from then on; backing out the way they
 *     came records nothing.
 * Because "crossed" is decided on exit rather than on first touch, depenetration, stepping back or re-entering the
 * zone can never lock a player out mid-doorway. Each door keeps its own list, keyed by PlayerState, so it works
 * for any number of players and doesn't depend on the Lobby assigning indices.
 *
 * Multiplayer: DoorState and CrossedPlayers replicate. The overlap logic runs on the server and on the moving
 * player's own client from the same geometry, so both agree on whether that player may pass; that keeps
 * CharacterMovement prediction from rubber-banding. The server's CrossedPlayers list is authoritative.
 * AI and other non-player actors never get passage (a one-way entrance is solid to them).
 */
UCLASS()
class FPSRL_API AFPSRLDoor : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLDoor();

	/** Current state. Only Open is walkable (for a one-way entrance: walkable once per player, inward). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_DoorState, Category = "Door")
	EDoorState DoorState = EDoorState::Open;

	/** Enables the per-player one-way behavior described on the class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	bool bIsOneWayEntrance = false;

	/** Doorway size, in cm. Blocker and DoorwayZone are sized from these in the editor and at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Size", meta = (ClampMin = "1"))
	float DoorWidth = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Size", meta = (ClampMin = "1"))
	float DoorHeight = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Size", meta = (ClampMin = "1"))
	float WallThickness = 20.f;

	/**
	 * How far the DoorwayZone reaches past each face of the wall. Must exceed a character's capsule diameter
	 * (~70 cm) so passage is only revoked once the player is fully clear of the wall.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Size", meta = (ClampMin = "80"))
	float ZoneDepth = 100.f;

	/**
	 * Glass tint per state, sent to DoorMesh's material as vector parameter "Tint" (RGB) and scalar "Opacity" (A),
	 * so the doorway always reads as a door. Materials without those parameters are left alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Look")
	FLinearColor OpenTint = FLinearColor(0.45f, 0.75f, 1.f, 0.12f);

	/** One-way door that is open: passable forward, solid behind you. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Look")
	FLinearColor OneWayTint = FLinearColor(0.3f, 1.f, 0.45f, 0.22f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Look")
	FLinearColor LockedTint = FLinearColor(1.f, 0.2f, 0.15f, 0.45f);

	/** Scale DoorMesh to the door size, assuming the engine's 100 cm cube. Turn off once real door art is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Size")
	bool bScaleMeshToDoorSize = true;

	// Server-only state changes. Callers (ArenaManager etc.) are already on the server. Names match the old
	// BP_Door functions so existing Blueprint calls keep working.

	/** Closed -> Open. Does nothing while Locked (Unlock first). */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Door")
	void Close();

	/** Solid for everyone, one-way passage included, until Unlock. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void Lock();

	/** Locked -> Open. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	void Unlock();

	/** True if this player has already gone through this one-way entrance. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool HasPlayerCrossed(const APlayerState* PlayerState) const;

	/** Cosmetic hook (sound, animation) fired on every machine when DoorState changes. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Door", meta = (DisplayName = "On Door State Changed"))
	void K2_OnDoorStateChanged(EDoorState NewState);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

private:
	void SetDoorState(EDoorState NewState);
	void ApplyDoorState();
	void UpdateGeometry();

	/** Let (or stop letting) one character's capsule move through the Blocker. */
	void SetPassage(ACharacter* Character, bool bAllow);
	void RevokeAllPassage();

	UFUNCTION()
	void OnRep_DoorState();

	UFUNCTION()
	void OnDoorwayBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> DoorRoot;

	/** Visual only; never collides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** The physical wall. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Blocker;

	/** Overlap-only zone straddling the wall; drives one-way passage. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> DoorwayZone;

	/** Editor-only arrow pointing into the room (+X). */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UArrowComponent> InsideDirection;

	/** Players who have crossed this one-way entrance. Server-authoritative; replicated so clients agree. */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<APlayerState>> CrossedPlayers;

	/** Characters currently allowed through the Blocker on this machine (local bookkeeping, not replicated). */
	TArray<TWeakObjectPtr<ACharacter>> CharactersWithPassage;
};
