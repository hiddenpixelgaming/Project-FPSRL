// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FPSRLDepthLayoutComponent.generated.h"

class UFPSRLDepthDefinition;
class UFPSRLRoomDefinition;
class ULevelStreamingDynamic;

/** One generated room: which room level, and where its origin sits in the Depth. */
USTRUCT(BlueprintType)
struct FFPSRLRoomPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	TObjectPtr<UFPSRLRoomDefinition> Room;

	UPROPERTY(BlueprintReadOnly, Category = "Depth")
	FTransform Transform;
};

DECLARE_MULTICAST_DELEGATE(FFPSRLLayoutReady);

/**
 * Procedural room sequencing for one Depth (lives on AFPSRLGameState).
 *
 * Server: rolls the room order from the Depth definition's pools (no room repeats within a Depth while alternatives
 * exist), chains each room's origin onto the previous room's exit (starting at the entry map's AFPSRLRoomConnector),
 * and replicates the result. Every machine then streams the same rooms in as level instances with the same names,
 * so the rooms' placed actors (doors, enemies, AFPSRLRoom, altars, portal) are the same networked actors everywhere.
 *
 * Phase 2 streams the whole Depth when it starts (rooms are small); OnLayoutReady fires on the server once every
 * room is loaded and visible, which is when the Depth may evaluate its required rooms.
 */
UCLASS(ClassGroup = (FPSRL))
class FPSRL_API UFPSRLDepthLayoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRLDepthLayoutComponent();

	UPROPERTY(ReplicatedUsing = OnRep_Placements, BlueprintReadOnly, Category = "Depth")
	TArray<FFPSRLRoomPlacement> Placements;

	/** Server: every generated room is streamed in and visible. */
	FFPSRLLayoutReady OnLayoutReady;

	/** Server: roll and stream this Depth's rooms. False if the Depth has no room pools (single handcrafted map). */
	bool BuildLayout(const UFPSRLDepthDefinition* Depth);

	bool IsLayoutPending() const { return bLayoutPending; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_Placements();

	UFUNCTION()
	void HandleRoomShown();

	static TArray<const UFPSRLRoomDefinition*> RollSequence(const UFPSRLDepthDefinition& Depth);
	FTransform FindEntryTransform() const;
	void StreamPlacements();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> StreamedRooms;

	bool bStreamed = false;
	bool bLayoutPending = false;
	int32 RoomsShown = 0;
};
