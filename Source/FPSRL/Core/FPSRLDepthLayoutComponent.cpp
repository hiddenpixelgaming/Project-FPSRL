// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLDepthLayoutComponent.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Data/FPSRLRunDefinition.h"
#include "Engine/LevelStreamingDynamic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLRoomConnector.h"
#include "FPSRL.h"

namespace
{
	/** Weighted pick without replacement; refills from the full pool only when every entry has been used. */
	const UFPSRLRoomDefinition* PickRoom(const TArray<TObjectPtr<UFPSRLRoomDefinition>>& Pool, TArray<const UFPSRLRoomDefinition*>& Used,
		TFunctionRef<bool(const UFPSRLRoomDefinition&)> Filter)
	{
		auto Collect = [&](bool bAllowUsed)
		{
			TArray<const UFPSRLRoomDefinition*> Candidates;
			for (const UFPSRLRoomDefinition* Room : Pool)
			{
				if (Room && !Room->Level.IsNull() && Room->SelectionWeight > 0.f && Filter(*Room) && (bAllowUsed || !Used.Contains(Room)))
				{
					Candidates.Add(Room);
				}
			}
			return Candidates;
		};

		TArray<const UFPSRLRoomDefinition*> Candidates = Collect(false);
		if (Candidates.IsEmpty())
		{
			Candidates = Collect(true);	// pool smaller than the Depth: repeats are unavoidable
		}
		if (Candidates.IsEmpty())
		{
			return nullptr;
		}

		float Total = 0.f;
		for (const UFPSRLRoomDefinition* Room : Candidates)
		{
			Total += Room->SelectionWeight;
		}
		float Roll = FMath::FRandRange(0.f, Total);
		for (const UFPSRLRoomDefinition* Room : Candidates)
		{
			Roll -= Room->SelectionWeight;
			if (Roll <= 0.f)
			{
				Used.Add(Room);
				return Room;
			}
		}
		Used.Add(Candidates.Last());
		return Candidates.Last();
	}

	bool AnyRoom(const UFPSRLRoomDefinition&) { return true; }
}

UFPSRLDepthLayoutComponent::UFPSRLDepthLayoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UFPSRLDepthLayoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, Placements);
}

// --- Generation (server) ---------------------------------------------------------------------------------------------

TArray<const UFPSRLRoomDefinition*> UFPSRLDepthLayoutComponent::RollSequence(const UFPSRLDepthDefinition& Depth)
{
	TArray<const UFPSRLRoomDefinition*> Used;

	// Required combat rooms.
	TArray<const UFPSRLRoomDefinition*> Combat;
	const int32 MinCombat = FMath::Max(Depth.MinCombatRooms, Depth.GuaranteedCombatRooms);
	const int32 CombatCount = FMath::RandRange(MinCombat, FMath::Max(MinCombat, Depth.MaxCombatRooms));
	for (int32 Index = 0; Index < CombatCount; ++Index)
	{
		if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.CombatRooms, Used, AnyRoom))
		{
			Combat.Add(Room);
		}
	}

	// Optional rooms: reward rooms by count, then altar / upgrade / merchant by chance or flag.
	TArray<const UFPSRLRoomDefinition*> Optional;
	auto AddOptional = [&](ERoomType Type)
	{
		if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.OptionalRooms, Used, [Type](const UFPSRLRoomDefinition& Candidate) { return Candidate.RoomType == Type; }))
		{
			Optional.Add(Room);
		}
	};
	const int32 RewardCount = FMath::RandRange(Depth.MinOptionalRooms, FMath::Max(Depth.MinOptionalRooms, Depth.MaxOptionalRooms));
	for (int32 Index = 0; Index < RewardCount; ++Index)
	{
		AddOptional(ERoomType::Reward);
	}
	if (FMath::FRand() < Depth.BoonAltarChance)
	{
		AddOptional(ERoomType::Boon);
	}
	if (FMath::FRand() < Depth.UpgradeStationChance)
	{
		AddOptional(ERoomType::Upgrade);
	}
	if (Depth.bHasMerchant)
	{
		AddOptional(ERoomType::Merchant);
	}

	// Mix optional rooms in after the first combat room, so the Depth never opens on a freebie.
	TArray<const UFPSRLRoomDefinition*> Sequence = Combat;
	for (const UFPSRLRoomDefinition* Room : Optional)
	{
		Sequence.Insert(Room, FMath::RandRange(FMath::Min(1, Sequence.Num()), Sequence.Num()));
	}

	if (Depth.bHasElite)
	{
		if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.EliteRooms, Used, AnyRoom))
		{
			Sequence.Add(Room);
		}
	}
	if (Depth.bHasBoss)
	{
		if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.BossRooms, Used, AnyRoom))
		{
			Sequence.Add(Room);
		}
	}
	if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.ExitRooms, Used, AnyRoom))
	{
		Sequence.Add(Room);
	}
	return Sequence;
}

FTransform UFPSRLDepthLayoutComponent::FindEntryTransform() const
{
	// Only the entry map's connector exists before any room is streamed in.
	for (TActorIterator<AFPSRLRoomConnector> It(GetWorld()); It; ++It)
	{
		FTransform Transform = It->GetActorTransform();
		Transform.SetScale3D(FVector::OneVector);
		return Transform;
	}
	UE_LOG(LogFPSRL, Warning, TEXT("[Depth] Entry map has no AFPSRLRoomConnector; generated rooms start at the world origin"));
	return FTransform::Identity;
}

bool UFPSRLDepthLayoutComponent::BuildLayout(const UFPSRLDepthDefinition* Depth)
{
	if (!GetOwner()->HasAuthority() || !Depth || !Depth->UsesRoomPools() || bStreamed)
	{
		return false;
	}

	FTransform Next = FindEntryTransform();
	Placements.Reset();
	FString Order;
	for (const UFPSRLRoomDefinition* Room : RollSequence(*Depth))
	{
		Placements.Add({ const_cast<UFPSRLRoomDefinition*>(Room), Next });
		Next = Room->ExitTransform * Next;	// this room's exit, in world space = where the next one starts
		Order += FString::Printf(TEXT(" %s"), *Room->GetName());
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Depth] %s layout (%d rooms):%s"), *Depth->GetName(), Placements.Num(), *Order);

	if (Placements.IsEmpty())
	{
		return false;
	}
	bLayoutPending = true;
	GetOwner()->ForceNetUpdate();
	StreamPlacements();
	return true;
}

// --- Streaming (every machine) -----------------------------------------------------------------------------------------

void UFPSRLDepthLayoutComponent::OnRep_Placements()
{
	StreamPlacements();
}

void UFPSRLDepthLayoutComponent::StreamPlacements()
{
	if (bStreamed || Placements.IsEmpty())
	{
		return;
	}
	bStreamed = true;

	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		const FFPSRLRoomPlacement& Placement = Placements[Index];
		if (!Placement.Room)
		{
			continue;
		}
		// Same instance name on server and clients -> the rooms' placed actors match up over the network.
		const FString InstanceName = FString::Printf(TEXT("FPSRLRoom_%02d_%s"), Index, *Placement.Room->GetName());
		bool bSuccess = false;
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(GetWorld(), Placement.Room->Level,
			Placement.Transform.GetLocation(), Placement.Transform.Rotator(), bSuccess, InstanceName);
		if (!bSuccess || !Streaming)
		{
			UE_LOG(LogFPSRL, Error, TEXT("[Depth] Failed to stream %s"), *Placement.Room->GetName());
			continue;
		}
		StreamedRooms.Add(Streaming);
		if (GetOwner()->HasAuthority())
		{
			Streaming->OnLevelShown.AddUniqueDynamic(this, &ThisClass::HandleRoomShown);
		}
	}

	if (GetOwner()->HasAuthority() && StreamedRooms.IsEmpty())
	{
		HandleRoomShown();	// nothing could be streamed: don't leave the Depth waiting forever
	}
}

void UFPSRLDepthLayoutComponent::HandleRoomShown()
{
	++RoomsShown;
	if (bLayoutPending && RoomsShown >= StreamedRooms.Num())
	{
		bLayoutPending = false;
		UE_LOG(LogFPSRL, Log, TEXT("[Depth] All %d rooms streamed in"), StreamedRooms.Num());
		OnLayoutReady.Broadcast();
	}
}

void UFPSRLDepthLayoutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ULevelStreamingDynamic* Streaming : StreamedRooms)
	{
		if (Streaming)
		{
			Streaming->OnLevelShown.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}
