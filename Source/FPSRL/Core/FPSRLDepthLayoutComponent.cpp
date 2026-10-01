// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLDepthLayoutComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Rooms/FPSRLExitPortal.h"
#include "Rooms/FPSRLHazardZone.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Data/FPSRLRunDefinition.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLFallVolume.h"
#include "Rooms/FPSRLRewardSpawnPoint.h"
#include "Rooms/FPSRLRoomConnector.h"
#include "TimerManager.h"
#include "FPSRL.h"

namespace FPSRLDepthLayout
{
	static TAutoConsoleVariable<FString> CVarForceCombatRoom(TEXT("fpsrl.Depth.ForceCombatRoom"), TEXT(""),
		TEXT("Testing: every combat room of the next Depth is this room definition (asset name, e.g. DA_Room_Arena01_SunkenPlaza; empty = random)."));
	static TAutoConsoleVariable<FString> CVarForceReward(TEXT("fpsrl.Depth.ForceReward"), TEXT(""),
		TEXT("Testing: every reward of the next Depth is this altar (BlessingAltar, UpgradeAltar, HealingAltar; empty = rolled)."));

}

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

	FString InstanceName(int32 Index, const UFPSRLRoomDefinition* Room)
	{
		// Same instance name on server and clients -> the rooms' placed actors match up over the network.
		return FString::Printf(TEXT("FPSRLRoom_%02d_%s"), Index, *GetNameSafe(Room));
	}
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
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, Window);
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, ReadyThrough);
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, RoomStates);
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, EncounterCleared);
	DOREPLIFETIME(UFPSRLDepthLayoutComponent, ActiveEncounterIndex);
}

bool UFPSRLDepthLayoutComponent::IsEncounterRoom(ERoomType Type)
{
	return Type == ERoomType::Combat || Type == ERoomType::Miniboss || Type == ERoomType::Boss;
}

int32 UFPSRLDepthLayoutComponent::GetRequiredEncounterCount() const
{
	int32 Count = 0;
	for (const FFPSRLRoomPlacement& Placement : Placements)
	{
		Count += (Placement.Room && IsEncounterRoom(Placement.Room->RoomType)) ? 1 : 0;
	}
	return Count;
}

int32 UFPSRLDepthLayoutComponent::GetClearedEncounterCount() const
{
	int32 Count = 0;
	for (const bool bCleared : EncounterCleared)
	{
		Count += bCleared ? 1 : 0;
	}
	return Count;
}

int32 UFPSRLDepthLayoutComponent::FindPlacementIndex(const AActor* Actor) const
{
	const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
	for (const TPair<int32, TObjectPtr<ULevelStreamingDynamic>>& Entry : StreamedRooms)
	{
		if (Level && Entry.Value && Entry.Value->GetLoadedLevel() == Level)
		{
			return Entry.Key;
		}
	}
	return INDEX_NONE;
}

// --- Sequence (server) -----------------------------------------------------------------------------------------------

TArray<FFPSRLRoomPlacement> UFPSRLDepthLayoutComponent::RollSequence(const UFPSRLDepthDefinition& Depth) const
{
	TArray<FFPSRLRoomPlacement> Sequence;
	TArray<const UFPSRLRoomDefinition*> Used;
	auto Add = [&Sequence](const UFPSRLRoomDefinition* Room, EFPSRLTraversalReward Reward = EFPSRLTraversalReward::None)
	{
		if (Room)
		{
			FFPSRLRoomPlacement& Placement = Sequence.AddDefaulted_GetRef();
			Placement.Room = const_cast<UFPSRLRoomDefinition*>(Room);
			Placement.Reward = Reward;
		}
	};
	const UFPSRLRunSubsystem* RunState = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr;
	const int32 DepthInArea = RunState ? RunState->GetDepthInArea() : 0;
	TArray<const UFPSRLRoomDefinition*> PreviousCombat;
	if (RunState)
	{
		PreviousCombat.Append(RunState->PreviousCombatRooms);
	}
	auto Eligible = [DepthInArea](const UFPSRLRoomDefinition& Room) { return Room.IsEligibleForDepth(DepthInArea); };
	const bool bTraversals = Depth.bTraversalBetweenCombatRooms && !Depth.TraversalRooms.IsEmpty();
	int32 TraversalIndex = 0;
	auto AddTraversal = [&](const FFPSRLTraversalRewardOdds& Odds)
	{
		Add(PickRoom(Depth.TraversalRooms, Used, AnyRoom), Odds.Roll());
		++TraversalIndex;
	};

	if (Depth.bHasPreparation)
	{
		Add(PickRoom(Depth.PreparationRooms, Used, AnyRoom));
	}

	// Combat rooms: a fixed count when Min == Max (Area 1 uses 4), a Traversal space between each pair.
	const int32 MinCombat = FMath::Max(Depth.MinCombatRooms, Depth.GuaranteedCombatRooms);
	const int32 CombatCount = FMath::RandRange(MinCombat, FMath::Max(MinCombat, Depth.MaxCombatRooms));
	for (int32 Index = 0; Index < CombatCount; ++Index)
	{
		if (Index > 0 && bTraversals)
		{
			AddTraversal(Depth.TraversalRewards.IsValidIndex(TraversalIndex) ? Depth.TraversalRewards[TraversalIndex] : Depth.DefaultTraversalReward);
		}
		// Authored arenas, picked at random: eligible for this Depth, not already in it, and (when the pool allows) not one
		// the previous Depth used.
		auto Fresh = [&](const UFPSRLRoomDefinition& Room) { return Room.IsEligibleForDepth(DepthInArea) && !PreviousCombat.Contains(&Room) && !Used.Contains(&Room); };
		const bool bHasFresh = Depth.CombatRooms.ContainsByPredicate([&](const UFPSRLRoomDefinition* Room)
		{
			return Room && !Room->Level.IsNull() && Room->SelectionWeight > 0.f && Fresh(*Room);
		});
		const UFPSRLRoomDefinition* Arena = bHasFresh ? PickRoom(Depth.CombatRooms, Used, Fresh) : PickRoom(Depth.CombatRooms, Used, Eligible);
		// Testing / reviewing one arena: fpsrl.Depth.ForceCombatRoom <room asset name> makes every combat room that one.
		const FString Forced = FPSRLDepthLayout::CVarForceCombatRoom.GetValueOnGameThread();
		if (!Forced.IsEmpty())
		{
			for (const UFPSRLRoomDefinition* Candidate : Depth.CombatRooms)
			{
				if (Candidate && Candidate->GetName() == Forced)
				{
					Arena = Candidate;
				}
			}
		}
		Add(Arena ? Arena : PickRoom(Depth.CombatRooms, Used, AnyRoom));
	}

	// Depths without traversal spaces keep the older optional rooms (Boon / Upgrade / Merchant rooms) mixed in.
	if (!bTraversals)
	{
		auto AddOptional = [&](ERoomType Type)
		{
			if (const UFPSRLRoomDefinition* Room = PickRoom(Depth.OptionalRooms, Used, [Type](const UFPSRLRoomDefinition& Candidate) { return Candidate.RoomType == Type; }))
			{
				FFPSRLRoomPlacement Placement;
				Placement.Room = const_cast<UFPSRLRoomDefinition*>(Room);
				Sequence.Insert(Placement, FMath::RandRange(FMath::Min(1, Sequence.Num()), Sequence.Num()));
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
	}

	if (Depth.bHasMiniboss)
	{
		Add(PickRoom(Depth.MinibossRooms, Used, AnyRoom));	// an extra encounter after the last combat room
	}
	if (Depth.bHasBoss)
	{
		if (Depth.bTraversalBeforeBoss && !Depth.TraversalRooms.IsEmpty() && !Sequence.IsEmpty())
		{
			AddTraversal(Depth.BossTraversalReward);	// the boss transition
		}
		Add(PickRoom(Depth.BossRooms, Used, AnyRoom));
	}
	Add(PickRoom(Depth.ExitRooms, Used, AnyRoom));
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
	if (!GetOwner()->HasAuthority() || !Depth || !Depth->UsesRoomPools() || HasLayout())
	{
		return false;
	}

	Placements = RollSequence(*Depth);
	// The rewards rolled for traversals are given at the end of the encounter room before them instead (user: nobody can
	// miss them on the way through), beside its exit door once it is cleared. Nothing is ever spawned in a traversal.
	for (int32 Index = 1; Index < Placements.Num(); ++Index)
	{
		FFPSRLRoomPlacement& Before = Placements[Index - 1];
		const FString ForcedReward = FPSRLDepthLayout::CVarForceReward.GetValueOnGameThread();
		if (!ForcedReward.IsEmpty() && Placements[Index].Reward != EFPSRLTraversalReward::None)
		{
			const int64 Value = StaticEnum<EFPSRLTraversalReward>()->GetValueByNameString(ForcedReward);
			Placements[Index].Reward = Value == INDEX_NONE ? Placements[Index].Reward : static_cast<EFPSRLTraversalReward>(Value);
		}
		if (Placements[Index].Reward != EFPSRLTraversalReward::None && Before.Room && IsEncounterRoom(Before.Room->RoomType)
			&& Before.Reward == EFPSRLTraversalReward::None)
		{
			Before.Reward = Placements[Index].Reward;
			Placements[Index].Reward = EFPSRLTraversalReward::None;
		}
	}
	// Rule (user): altars only ever at the end of combat rooms; no other room gets one (the Preparation room has its own,
	// placed in its level). A reward with no encounter room before it is dropped.
	for (FFPSRLRoomPlacement& Placement : Placements)
	{
		if (Placement.Reward != EFPSRLTraversalReward::None && !(Placement.Room && IsEncounterRoom(Placement.Room->RoomType)))
		{
			Placement.Reward = EFPSRLTraversalReward::None;
		}
	}
	// The next Depth avoids these arenas when it can (no back-to-back repeats).
	if (UFPSRLRunSubsystem* RunState = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UFPSRLRunSubsystem>() : nullptr)
	{
		RunState->PreviousCombatRooms.Reset();
		for (const FFPSRLRoomPlacement& Placement : Placements)
		{
			if (Placement.Room && Placement.Room->RoomType == ERoomType::Combat)
			{
				RunState->PreviousCombatRooms.Add(Placement.Room);
			}
		}
	}
	FTransform Next = FindEntryTransform();
	FString Order;
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		FFPSRLRoomPlacement& Placement = Placements[Index];
		Placement.Transform = Next;
		Next = Placement.Room->ExitTransform * Next;	// this room's exit, in world space = where the next one starts
		Order += FString::Printf(TEXT(" %s"), *Placement.Room->GetName());
		if (Placement.Reward != EFPSRLTraversalReward::None)
		{
			Order += FString::Printf(TEXT("[%s]"), *UEnum::GetDisplayValueAsText(Placement.Reward).ToString());
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Depth] %s layout (%d rooms, %d encounters):%s"), *Depth->GetName(), Placements.Num(), GetRequiredEncounterCount(), *Order);
	if (Placements.IsEmpty())
	{
		return false;
	}

	RoomStates.Init(EFPSRLRoomState::Unloaded, Placements.Num());
	EncounterCleared.Init(false, Placements.Num());
	bLayoutPending = true;
	if (AFPSRLGameState* GameState = GetOwner<AFPSRLGameState>())
	{
		GameState->OnPlayerLeft.AddUObject(this, &ThisClass::RecomputeReadiness);
	}
	GetWorld()->GetTimerManager().SetTimer(OccupancyTimer, this, &ThisClass::CheckOccupancy,
		UFPSRLRunSettings::Get().RoomUnloadCheckInterval, true);

	// One wide fall volume under the whole Depth: every room origin and exit, plus a generous margin.
	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	FBox Area(ForceInit);
	for (const FFPSRLRoomPlacement& Placement : Placements)
	{
		Area += Placement.Transform.GetLocation();
		Area += (Placement.Room->ExitTransform * Placement.Transform).GetLocation();
	}
	Area += FindEntryTransform().GetLocation();
	const FVector Min(Area.Min.X - Settings.FallVolumeMargin, Area.Min.Y - Settings.FallVolumeMargin, Area.Min.Z - Settings.FallVolumeDepth - 4000.f);
	const FVector Max(Area.Max.X + Settings.FallVolumeMargin, Area.Max.Y + Settings.FallVolumeMargin, Area.Min.Z - Settings.FallVolumeDepth);
	if (AFPSRLFallVolume* Volume = GetWorld()->SpawnActor<AFPSRLFallVolume>())
	{
		Volume->Cover(FBox(Min, Max));
		FallVolume = Volume;
	}

	UpdateWindow();
	return true;
}

bool UFPSRLDepthLayoutComponent::FindSafeSpot(const FVector& FellFrom, FTransform& OutSpot) const
{
	// The loaded room whose floor plan is closest to where the player went over the edge.
	int32 Best = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (const TPair<int32, FBox>& Entry : PlacementBounds)
	{
		if (!Placements.IsValidIndex(Entry.Key) || !Entry.Value.IsValid)
		{
			continue;
		}
		const FVector Center = Entry.Value.GetCenter();
		const FVector Extent = Entry.Value.GetExtent();
		const double DX = FMath::Max(0.0, FMath::Abs(FellFrom.X - Center.X) - Extent.X);
		const double DY = FMath::Max(0.0, FMath::Abs(FellFrom.Y - Center.Y) - Extent.Y);
		const double Distance = DX * DX + DY * DY + FVector::DistSquared2D(FellFrom, Center) * 1e-6;	// inside several: nearest centre
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Entry.Key;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	const FTransform& Room = Placements[Best].Transform;
	OutSpot = FTransform(Room.Rotator(), Room.TransformPosition(FVector(200.f, 0.f, 120.f)));	// just inside its entrance
	return true;
}

// --- Window (server decides) -------------------------------------------------------------------------------------------

void UFPSRLDepthLayoutComponent::UpdateWindow()
{
	if (!GetOwner()->HasAuthority() || Placements.IsEmpty())
	{
		return;
	}

	// Load up to one room past the first encounter still to clear (its traversal, or the exit room); once everything is
	// cleared, the whole rest of the Depth. Rooms behind the rearmost player are dropped (KeepFrom, from CheckOccupancy).
	int32 FirstUncleared = INDEX_NONE;
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		if (Placements[Index].Room && IsEncounterRoom(Placements[Index].Room->RoomType) && !EncounterCleared[Index])
		{
			FirstUncleared = Index;
			break;
		}
	}
	FFPSRLStreamWindow NewWindow;
	NewWindow.Last = FirstUncleared == INDEX_NONE ? Placements.Num() - 1 : FMath::Min(Placements.Num() - 1, FirstUncleared + 1);
	NewWindow.First = FMath::Clamp(FMath::Max(KeepFrom, Window.First), 0, NewWindow.Last);
	if (NewWindow == Window)
	{
		return;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Depth] Loaded rooms: %d..%d (was %d..%d)"), NewWindow.First, NewWindow.Last, Window.First, Window.Last);
	Window = NewWindow;
	GetOwner()->ForceNetUpdate();
	ApplyWindow();
	RecomputeReadiness();
}

// --- Streaming (every machine) -----------------------------------------------------------------------------------------

void UFPSRLDepthLayoutComponent::OnRep_Placements()
{
	ApplyWindow();
}

void UFPSRLDepthLayoutComponent::OnRep_Window()
{
	ApplyWindow();
}

void UFPSRLDepthLayoutComponent::ApplyWindow()
{
	const bool bServer = GetOwner()->HasAuthority();

	// Unload rooms that left the window (never reloaded: progress is forward only).
	TArray<int32> ToUnload;
	for (const TPair<int32, TObjectPtr<ULevelStreamingDynamic>>& Entry : StreamedRooms)
	{
		if (!Window.Contains(Entry.Key))
		{
			ToUnload.Add(Entry.Key);
		}
	}
	for (const int32 Index : ToUnload)
	{
		if (bServer)
		{
			OnPlacementUnloading(Index);
		}
		if (ULevelStreamingDynamic* Streaming = StreamedRooms.FindAndRemoveChecked(Index))
		{
			Streaming->OnLevelShown.RemoveAll(this);
			Streaming->SetIsRequestingUnloadAndRemoval(true);
		}
		ShownHere.Remove(Index);
		if (bServer)
		{
			SetRoomState(Index, EFPSRLRoomState::Unloaded);
		}
		UE_LOG(LogFPSRL, Log, TEXT("[Depth] Unloaded room %d"), Index);
	}

	// Load rooms that entered it.
	for (int32 Index = Window.First; Index <= Window.Last; ++Index)
	{
		if (!Placements.IsValidIndex(Index) || !Placements[Index].Room || StreamedRooms.Contains(Index))
		{
			continue;
		}
		const FFPSRLRoomPlacement& Placement = Placements[Index];
		bool bSuccess = false;
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(GetWorld(), Placement.Room->Level,
			Placement.Transform.GetLocation(), Placement.Transform.Rotator(), bSuccess, InstanceName(Index, Placement.Room));
		if (!bSuccess || !Streaming)
		{
			UE_LOG(LogFPSRL, Error, TEXT("[Depth] Failed to stream %s"), *Placement.Room->GetName());
			continue;
		}
		StreamedRooms.Add(Index, Streaming);
		Streaming->OnLevelShown.AddUniqueDynamic(this, &ThisClass::HandleStreamingChanged);
		if (bServer)
		{
			SetRoomState(Index, EFPSRLRoomState::Loading);
		}
	}
}

void UFPSRLDepthLayoutComponent::HandleStreamingChanged()
{
	const bool bServer = GetOwner()->HasAuthority();
	for (const TPair<int32, TObjectPtr<ULevelStreamingDynamic>>& Entry : StreamedRooms)
	{
		if (!Entry.Value || !Entry.Value->IsLevelVisible() || ShownHere.Contains(Entry.Key))
		{
			continue;
		}
		ShownHere.Add(Entry.Key);
		if (bServer)
		{
			OnPlacementShownOnServer(Entry.Key);
		}
	}
	if (!bServer)
	{
		ResendUnconfirmedRooms();	// the server opens gates only once every machine has the room
		return;
	}

	RecomputeReadiness();
	if (bLayoutPending)
	{
		bool bAllShown = true;
		for (int32 Index = Window.First; Index <= Window.Last; ++Index)
		{
			bAllShown &= ShownHere.Contains(Index);
		}
		if (bAllShown)
		{
			bLayoutPending = false;
			UE_LOG(LogFPSRL, Log, TEXT("[Depth] First rooms streamed in (%d..%d)"), Window.First, Window.Last);
			OnLayoutReady.Broadcast();
		}
	}
}

void UFPSRLDepthLayoutComponent::OnRep_ReadyThrough()
{
	ResendUnconfirmedRooms();
	OnReadinessChanged.Broadcast();
}

void UFPSRLDepthLayoutComponent::ResendUnconfirmedRooms()
{
	if (GetOwner()->HasAuthority())
	{
		return;
	}
	// Report every room loaded here that the server hasn't counted yet (ReadyThrough is below it). A report sent while
	// this client's controller is still being swapped after travel has no connection and is dropped by the engine, so
	// wait for a connected controller and keep retrying once a second until the server has counted them all.
	AFPSRLPlayerController* LocalPC = Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController());
	const bool bConnected = LocalPC && LocalPC->IsLocalController() && LocalPC->GetNetConnection();
	bool bPending = false;
	for (const int32 Index : ShownHere)
	{
		if (Index > ReadyThrough)
		{
			bPending = true;
			if (bConnected)
			{
				LocalPC->ServerReportRoomShown(Index);
			}
		}
	}
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	if (bPending && !Timers.IsTimerActive(ReportRetryTimer))
	{
		Timers.SetTimer(ReportRetryTimer, this, &ThisClass::ResendUnconfirmedRooms, 1.f, true);
	}
	else if (!bPending)
	{
		Timers.ClearTimer(ReportRetryTimer);
	}
}

// --- Readiness, occupancy, rewards (server) ----------------------------------------------------------------------------

void UFPSRLDepthLayoutComponent::ReportRoomShown(APlayerController* Player, int32 Index)
{
	if (GetOwner()->HasAuthority() && Player)
	{
		ShownByClient.FindOrAdd(Player).Add(Index);
		RecomputeReadiness();
	}
}

void UFPSRLDepthLayoutComponent::RecomputeReadiness()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	auto ContiguousFrom = [this](const TSet<int32>& Shown)
	{
		int32 Last = Window.First - 1;
		while (Last + 1 <= Window.Last && Shown.Contains(Last + 1))
		{
			++Last;
		}
		return Last;
	};

	// Every connected player's machine must have the room (the listen host counts through the server's own set).
	int32 Ready = ContiguousFrom(ShownHere);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && !PC->IsLocalController())
		{
			const TSet<int32>* Shown = ShownByClient.Find(PC);
			Ready = FMath::Min(Ready, Shown ? ContiguousFrom(*Shown) : Window.First - 1);
		}
	}
	for (auto It = ShownByClient.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();	// left the game
		}
	}
	if (Ready != ReadyThrough)
	{
		ReadyThrough = Ready;
		GetOwner()->ForceNetUpdate();
		OnReadinessChanged.Broadcast();
	}
}

void UFPSRLDepthLayoutComponent::CheckOccupancy()
{
	UpdatePlayerRooms();

	// Which room is each player in? Anyone outside every loaded room (e.g. still in the entry map) keeps everything.
	int32 Rearmost = MAX_int32;
	bool bAnyPlayer = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;	// dead or spectating: doesn't hold rooms open
		}
		bAnyPlayer = true;
		int32 Found = INDEX_NONE;
		for (const TPair<int32, FBox>& Entry : PlacementBounds)
		{
			if (ShownHere.Contains(Entry.Key) && Entry.Value.ExpandBy(150.f).IsInside(Pawn->GetActorLocation()))
			{
				Found = Found == INDEX_NONE ? Entry.Key : FMath::Min(Found, Entry.Key);
			}
		}
		Rearmost = FMath::Min(Rearmost, Found);	// INDEX_NONE (-1) keeps everything loaded
	}
	if (bAnyPlayer && Rearmost > 0 && Rearmost - 1 > KeepFrom)
	{
		KeepFrom = Rearmost - 1;	// keep the room right behind the rearmost player, drop the rest
		UpdateWindow();
	}
}

void UFPSRLDepthLayoutComponent::OnPlacementShownOnServer(int32 Index)
{
	ULevelStreamingDynamic* Streaming = StreamedRooms.FindRef(Index);
	ULevel* Level = Streaming ? Streaming->GetLoadedLevel() : nullptr;
	if (!Level)
	{
		return;
	}

	// The room's extent, for knowing when every player has left it.
	FBox Bounds(ForceInit);
	AFPSRLRewardSpawnPoint* RewardPoint = nullptr;
	for (AActor* Actor : Level->Actors)
	{
		if (!Actor)
		{
			continue;
		}
		RewardPoint = RewardPoint ? RewardPoint : Cast<AFPSRLRewardSpawnPoint>(Actor);
		if (Actor->IsA<AFPSRLRoomConnector>())
		{
			continue;	// its arrow reaches into the next room
		}
		const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
		if (ActorBox.IsValid && ActorBox.GetExtent().GetMax() < 20000.f)
		{
			Bounds += ActorBox;
		}
	}
	PlacementBounds.Add(Index, Bounds);

	// A traversal's reward: spawn what the data rolled (nothing for None, and nothing yet for Future).
	const FFPSRLRoomPlacement& Placement = Placements[Index];
	const bool bEncounterRoom = Placement.Room && IsEncounterRoom(Placement.Room->RoomType);
	if (Placement.Reward != EFPSRLTraversalReward::None && !bEncounterRoom)	// an encounter room's comes when it is cleared
	{
		UClass* RewardClass = UFPSRLRunSettings::Get().TraversalRewardClasses.FindRef(Placement.Reward).LoadSynchronous();
		if (RewardClass && RewardPoint)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			RewardActors.Add(Index, GetWorld()->SpawnActor<AActor>(RewardClass, RewardPoint->GetActorTransform(), Params));
			UE_LOG(LogFPSRL, Log, TEXT("[Depth] Room %d: spawned %s"), Index, *UEnum::GetDisplayValueAsText(Placement.Reward).ToString());
		}
		else
		{
			UE_LOG(LogFPSRL, Warning, TEXT("[Depth] Room %d: no %s (class set: %d, reward point: %d)"), Index,
				*UEnum::GetDisplayValueAsText(Placement.Reward).ToString(), RewardClass != nullptr, RewardPoint != nullptr);
		}
	}

	const bool bEncounter = Placement.Room && IsEncounterRoom(Placement.Room->RoomType);
	SetRoomState(Index, bEncounter && EncounterCleared[Index] ? EFPSRLRoomState::Completed
		: (RoomStates[Index] == EFPSRLRoomState::Combat ? EFPSRLRoomState::Combat : EFPSRLRoomState::Active));
}

void UFPSRLDepthLayoutComponent::OnPlacementUnloading(int32 Index)
{
	SetRoomState(Index, EFPSRLRoomState::Unloading);

	// What the server spawned into the room: its reward, and the bodies of its enemies.
	if (AActor* Reward = RewardActors.FindRef(Index).Get())
	{
		Reward->Destroy();
	}
	RewardActors.Remove(Index);
	if (const FBox* Bounds = PlacementBounds.Find(Index))
	{
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			const UFPSRLHealthComponent* Health = It->FindComponentByClass<UFPSRLHealthComponent>();
			if (!It->IsPlayerControlled() && Health && Health->IsDead() && Bounds->IsInside(It->GetActorLocation()))
			{
				It->Destroy();
			}
		}
	}
	PlacementBounds.Remove(Index);
}

void UFPSRLDepthLayoutComponent::SetRoomState(int32 Index, EFPSRLRoomState State)
{
	if (RoomStates.IsValidIndex(Index) && RoomStates[Index] != State)
	{
		RoomStates[Index] = State;
		GetOwner()->ForceNetUpdate();
		OnRoomStateChanged.Broadcast();
	}
}

// --- Encounters (server) -----------------------------------------------------------------------------------------------

void UFPSRLDepthLayoutComponent::NotifyEncounterStarted(const AActor* RoomActor)
{
	const int32 Index = FindPlacementIndex(RoomActor);
	SetRoomState(Index, EFPSRLRoomState::Combat);
	if (!GetOwner()->HasAuthority() || !Placements.IsValidIndex(Index))
	{
		return;
	}
	// The expedition's active room moves on; teammates still behind get their catch-up offer a moment later (those a
	// few steps behind arrive by themselves first). The encounter itself never waits.
	ActiveEncounterIndex = Index;
	GetOwner()->ForceNetUpdate();
	if (IsCatchUpAllowedFor(Index))
	{
		GetWorld()->GetTimerManager().SetTimer(CatchUpOfferTimer, FTimerDelegate::CreateUObject(this, &ThisClass::OfferCatchUp, Index),
			FMath::Max(0.01f, UFPSRLRunSettings::Get().CatchUpOfferDelay), false);
	}
}

void UFPSRLDepthLayoutComponent::NotifyEncounterCleared(const AActor* RoomActor)
{
	const int32 Index = FindPlacementIndex(RoomActor);
	if (!GetOwner()->HasAuthority() || !EncounterCleared.IsValidIndex(Index) || EncounterCleared[Index])
	{
		return;
	}
	EncounterCleared[Index] = true;
	SetRoomState(Index, EFPSRLRoomState::Completed);

	// Its reward (Blessing / Upgrade Altar), beside the exit door, so everyone passes it on the way out.
	const EFPSRLTraversalReward Reward = Placements.IsValidIndex(Index) ? Placements[Index].Reward : EFPSRLTraversalReward::None;
	if (Reward != EFPSRLTraversalReward::None && Placements.IsValidIndex(Index + 1) && !RewardActors.Contains(Index))
	{
		UClass* RewardClass = UFPSRLRunSettings::Get().TraversalRewardClasses.FindRef(Reward).LoadSynchronous();
		const FTransform& Exit = Placements[Index + 1].Transform;	// this room's exit = where the next one starts
		const FTransform At(Exit.Rotator(), Exit.TransformPosition(UFPSRLRunSettings::Get().EncounterRewardOffsetFromExit));
		if (RewardClass)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			RewardActors.Add(Index, GetWorld()->SpawnActor<AActor>(RewardClass, At, Params));
			UE_LOG(LogFPSRL, Log, TEXT("[Depth] Room %d cleared: spawned %s by its exit at %s"), Index,
				*UEnum::GetDisplayValueAsText(Reward).ToString(), *At.GetLocation().ToCompactString());
		}
	}
	ExpireCatchUpOffers(Index, false);	// nothing left to catch up to
	UpdateWindow();	// start loading what comes next
}

void UFPSRLDepthLayoutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OccupancyTimer);
		World->GetTimerManager().ClearTimer(CatchUpOfferTimer);
		World->GetTimerManager().ClearTimer(ReportRetryTimer);
	}
	for (const TPair<int32, TObjectPtr<ULevelStreamingDynamic>>& Entry : StreamedRooms)
	{
		if (Entry.Value)
		{
			Entry.Value->OnLevelShown.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// --- Expedition progress: player rooms, catch-up (server) ---------------------------------------------------------------

FText UFPSRLDepthLayoutComponent::GetRoomDisplayName(int32 Index) const
{
	const UFPSRLRoomDefinition* Room = Placements.IsValidIndex(Index) ? Placements[Index].Room.Get() : nullptr;
	if (!Room)
	{
		return NSLOCTEXT("FPSRL", "UnknownRoom", "the next room");
	}
	return Room->DisplayName.IsEmpty() ? UEnum::GetDisplayValueAsText(Room->RoomType) : Room->DisplayName;
}

int32 UFPSRLDepthLayoutComponent::FindRoomAt(const FVector& Location) const
{
	// Rooms touch at their connectors, so a doorway can be inside two: the later one wins (a player in a doorway is
	// already arriving, never "behind").
	int32 Found = INDEX_NONE;
	for (const TPair<int32, FBox>& Entry : PlacementBounds)
	{
		if (ShownHere.Contains(Entry.Key) && Entry.Value.IsValid && Entry.Value.ExpandBy(50.f).IsInside(Location))
		{
			Found = FMath::Max(Found, Entry.Key);
		}
	}
	return Found;
}

void UFPSRLDepthLayoutComponent::UpdatePlayerRooms()
{
	if (!GetOwner()->HasAuthority() || Placements.IsEmpty())
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const int32 Room = Pawn ? FindRoomAt(Pawn->GetActorLocation()) : INDEX_NONE;
		if (!PS || Room == INDEX_NONE || Room == PS->ExpeditionRoomIndex)
		{
			continue;	// dead, between rooms or falling: keeps the last known room
		}
		const int32 Previous = PS->ExpeditionRoomIndex;
		PS->ExpeditionRoomIndex = Room;
		PS->ForceNetUpdate();
		UE_LOG(LogFPSRL, Log, TEXT("[Progress] %s: room %d -> %d (%s)"), *PS->GetPlayerName(), Previous, Room, *GetRoomDisplayName(Room).ToString());

		// The first player into a new room tells the others (awareness only: nobody has to follow).
		if (Room > FurthestRoomReached)
		{
			FurthestRoomReached = Room;
			if (UFPSRLRunSettings::Get().bAnnouncePlayerAdvance && Room > 0)
			{
				const FText Message = FText::Format(NSLOCTEXT("FPSRL", "PlayerMovedAhead", "{0} moved ahead to {1}"),
					FText::FromString(PS->GetPlayerName()), GetRoomDisplayName(Room));
				for (FConstPlayerControllerIterator Other = GetWorld()->GetPlayerControllerIterator(); Other; ++Other)
				{
					AFPSRLPlayerController* OtherPC = Cast<AFPSRLPlayerController>(Other->Get());
					if (OtherPC && OtherPC != PC)
					{
						OtherPC->ClientShowNotice(Message);
					}
				}
			}
		}
	}
	ExpireCatchUpOffers(ActiveEncounterIndex, true);	// got there on foot
}

bool UFPSRLDepthLayoutComponent::IsCatchUpAllowedFor(int32 RoomIndex) const
{
	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	const UFPSRLRoomDefinition* Room = Placements.IsValidIndex(RoomIndex) ? Placements[RoomIndex].Room.Get() : nullptr;
	if (!Settings.bCatchUpEnabled || !Room)
	{
		return false;
	}
	switch (Room->RoomType)
	{
	case ERoomType::Combat:		return Settings.bCatchUpToCombatRooms;
	case ERoomType::Miniboss:	return Settings.bCatchUpToMiniboss;
	case ERoomType::Boss:		return Settings.bCatchUpToFinalBoss;
	default:					return false;
	}
}

void UFPSRLDepthLayoutComponent::OfferCatchUp(int32 RoomIndex)
{
	if (!GetOwner()->HasAuthority() || RoomIndex != ActiveEncounterIndex || !IsCatchUpAllowedFor(RoomIndex)
		|| !EncounterCleared.IsValidIndex(RoomIndex) || EncounterCleared[RoomIndex])
	{
		return;	// moved on, or already over
	}
	UpdatePlayerRooms();

	// Named in the offer: someone who is in the room.
	FString Leader;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It && Leader.IsEmpty(); ++It)
	{
		const AFPSRLPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		Leader = PS && PS->ExpeditionRoomIndex >= RoomIndex ? PS->GetPlayerName() : FString();
	}
	const FText Message = FText::Format(NSLOCTEXT("FPSRL", "CatchUpOffer", "{0} is engaged in combat"),
		Leader.IsEmpty() ? NSLOCTEXT("FPSRL", "ATeammate", "A teammate") : FText::FromString(Leader), GetRoomDisplayName(RoomIndex));

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(It->Get());
		AFPSRLPlayerState* PS = PC ? PC->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		if (!PS || PS->ExpeditionRoomIndex >= RoomIndex)
		{
			continue;	// already there
		}
		if (!UFPSRLHealthComponent::IsPawnUp(PC->GetPawn()))
		{
			UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] %s is behind (room %d) but down or dead: no offer"), *PS->GetPlayerName(), PS->ExpeditionRoomIndex);
			continue;
		}
		FCatchUpOffer& Offer = CatchUpOffers.FindOrAdd(PS);
		if (Offer.RoomIndex == RoomIndex)
		{
			continue;	// one offer per player per room
		}
		Offer.RoomIndex = RoomIndex;
		Offer.Response = ECatchUpResponse::Pending;
		PC->ClientCatchUpOffer(RoomIndex, Message, GetRoomDisplayName(RoomIndex));
		UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] offered room %d (%s) to %s, who is in room %d"), RoomIndex,
			*GetRoomDisplayName(RoomIndex).ToString(), *PS->GetPlayerName(), PS->ExpeditionRoomIndex);
	}
}

void UFPSRLDepthLayoutComponent::ExpireCatchUpOffers(int32 RoomIndex, bool bOnlyArrived)
{
	if (RoomIndex == INDEX_NONE)
	{
		return;
	}
	for (TPair<TWeakObjectPtr<APlayerState>, FCatchUpOffer>& Entry : CatchUpOffers)
	{
		const AFPSRLPlayerState* PS = Cast<AFPSRLPlayerState>(Entry.Key.Get());
		FCatchUpOffer& Offer = Entry.Value;
		if (!PS || Offer.RoomIndex != RoomIndex || Offer.Response != ECatchUpResponse::Pending
			|| (bOnlyArrived && PS->ExpeditionRoomIndex < RoomIndex))
		{
			continue;
		}
		Offer.Response = ECatchUpResponse::Expired;
		if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(PS->GetPlayerController()))
		{
			PC->ClientCatchUpResolved(RoomIndex, FText::GetEmpty());
		}
		UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] %s's offer for room %d expired (%s)"), *PS->GetPlayerName(), RoomIndex,
			bOnlyArrived ? TEXT("arrived on foot") : TEXT("room cleared"));
	}
}

bool UFPSRLDepthLayoutComponent::FindCatchUpSpot(int32 RoomIndex, const APawn* Pawn, FTransform& OutSpot, FString& OutReason) const
{
	UWorld* World = GetWorld();
	const UFPSRLRunSettings& Settings = UFPSRLRunSettings::Get();
	const FTransform& Room = Placements[RoomIndex].Transform;
	const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 42.f;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 96.f;
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSRLCatchUpSpot), false, Pawn);
	OutReason = TEXT("no candidate spots");

	for (const FVector& Offset : Settings.CatchUpSpawnOffsets)
	{
		FVector Spot = Room.TransformPosition(Offset);
		FNavLocation OnNav;
		if (Nav && !Nav->ProjectPointToNavigation(Spot, OnNav, FVector(100.f, 100.f, 250.f)))
		{
			OutReason = FString::Printf(TEXT("%s: no navmesh"), *Spot.ToCompactString());
			continue;
		}
		if (Nav)
		{
			Spot = OnNav.Location + FVector(0.f, 0.f, HalfHeight + 5.f);
		}
		if (World->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		{
			OutReason = FString::Printf(TEXT("%s: occupied (geometry, a player or an enemy)"), *Spot.ToCompactString());
			continue;
		}
		const FVector Feet = Spot - FVector(0.f, 0.f, HalfHeight);
		bool bUnsafe = false;
		for (TActorIterator<AFPSRLHazardZone> Hazard(World); Hazard && !bUnsafe; ++Hazard)
		{
			bUnsafe = Hazard->GetZone() && Hazard->GetZone()->Bounds.GetBox().ExpandBy(Radius).IsInside(Feet);
		}
		if (bUnsafe)
		{
			OutReason = FString::Printf(TEXT("%s: in a hazard"), *Spot.ToCompactString());
			continue;
		}
		for (TActorIterator<APawn> Other(World); Other && !bUnsafe; ++Other)
		{
			const UFPSRLHealthComponent* Health = Other->IsPlayerControlled() ? nullptr : Other->FindComponentByClass<UFPSRLHealthComponent>();
			bUnsafe = Health && !Health->IsDead() && FVector::Dist(Other->GetActorLocation(), Spot) < Settings.CatchUpEnemyClearance;
		}
		if (bUnsafe)
		{
			OutReason = FString::Printf(TEXT("%s: an enemy is too close"), *Spot.ToCompactString());
			continue;
		}
		OutSpot = FTransform(Room.Rotator(), Spot);
		return true;
	}
	return false;
}

void UFPSRLDepthLayoutComponent::RequestCatchUp(APlayerController* Player, int32 RoomIndex)
{
	AFPSRLPlayerState* PS = Player ? Player->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Player);
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (!GetOwner()->HasAuthority() || !PS || !PC)
	{
		return;
	}
	// Final: the offer is gone. Not final: the offer stays open and the player can try again.
	auto Refuse = [this, PS, PC, RoomIndex](const FString& Why, bool bFinal)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] %s's request for room %d refused (%s): %s"), *PS->GetPlayerName(), RoomIndex,
			bFinal ? TEXT("final") : TEXT("try again"), *Why);
		if (bFinal)
		{
			FCatchUpOffer* Open = CatchUpOffers.Find(PS);
			if (Open && Open->RoomIndex == RoomIndex && Open->Response == ECatchUpResponse::Pending)
			{
				Open->Response = ECatchUpResponse::Expired;
			}
			PC->ClientCatchUpResolved(RoomIndex, NSLOCTEXT("FPSRL", "CatchUpUnavailable", "Catch-up is no longer available"));
		}
		else
		{
			PC->ClientShowNotice(NSLOCTEXT("FPSRL", "CatchUpNotNow", "Can't catch up this moment, try again"));
		}
	};

	FCatchUpOffer* Offer = CatchUpOffers.Find(PS);
	if (!Offer || Offer->RoomIndex != RoomIndex || Offer->Response != ECatchUpResponse::Pending)
	{
		Refuse(TEXT("no open offer for that room"), true);
		return;
	}
	if (RoomIndex != ActiveEncounterIndex || !IsCatchUpAllowedFor(RoomIndex))
	{
		Refuse(TEXT("not the active room"), true);
		return;
	}
	if (EncounterCleared[RoomIndex])
	{
		Refuse(TEXT("its encounter is already over"), true);
		return;
	}
	UpdatePlayerRooms();
	if (PS->ExpeditionRoomIndex >= RoomIndex)
	{
		Refuse(TEXT("already there"), true);
		return;
	}
	for (TActorIterator<AFPSRLExitPortal> Portal(GetWorld()); Portal; ++Portal)
	{
		if (Portal->PortalState == EPortalState::Used)
		{
			Refuse(TEXT("the party is travelling"), true);
			return;
		}
	}
	if (!UFPSRLHealthComponent::IsPawnUp(Pawn))
	{
		Refuse(TEXT("down or dead"), false);
		return;
	}
	if (!ShownHere.Contains(RoomIndex) || ReadyThrough < RoomIndex)
	{
		Refuse(TEXT("the room isn't loaded on every machine yet"), false);
		return;
	}
	FTransform Spot;
	FString Why;
	if (!FindCatchUpSpot(RoomIndex, Pawn, Spot, Why))
	{
		Refuse(FString::Printf(TEXT("no safe spot (last: %s)"), *Why), false);
		return;
	}
	if (!Pawn->TeleportTo(Spot.GetLocation(), Spot.Rotator()))
	{
		Refuse(TEXT("the teleport was blocked"), false);
		return;
	}
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	PC->ClientSetRotation(Spot.Rotator());
	Offer->Response = ECatchUpResponse::Accepted;
	PS->ExpeditionRoomIndex = RoomIndex;
	PS->ForceNetUpdate();
	PC->ClientCatchUpResolved(RoomIndex, FText::Format(NSLOCTEXT("FPSRL", "CaughtUp", "Caught up: {0}"), GetRoomDisplayName(RoomIndex)));
	UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] %s moved to room %d's entrance at %s"), *PS->GetPlayerName(), RoomIndex, *Spot.GetLocation().ToCompactString());
}

void UFPSRLDepthLayoutComponent::DeclineCatchUp(APlayerController* Player, int32 RoomIndex)
{
	AFPSRLPlayerState* PS = Player ? Player->GetPlayerState<AFPSRLPlayerState>() : nullptr;
	FCatchUpOffer* Offer = PS ? CatchUpOffers.Find(PS) : nullptr;
	if (!GetOwner()->HasAuthority() || !Offer || Offer->RoomIndex != RoomIndex || Offer->Response != ECatchUpResponse::Pending)
	{
		return;
	}
	Offer->Response = ECatchUpResponse::Declined;	// this offer only: a later room can offer again
	if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Player))
	{
		PC->ClientCatchUpResolved(RoomIndex, FText::GetEmpty());
	}
	UE_LOG(LogFPSRL, Log, TEXT("[CatchUp] %s declined room %d"), *PS->GetPlayerName(), RoomIndex);
}

FString UFPSRLDepthLayoutComponent::DescribeProgress() const
{
	FString Text = FString::Printf(TEXT("active room %d (%s%s), furthest reached %d"), ActiveEncounterIndex,
		*GetRoomDisplayName(ActiveEncounterIndex).ToString(),
		EncounterCleared.IsValidIndex(ActiveEncounterIndex) && EncounterCleared[ActiveEncounterIndex] ? TEXT(", cleared") : TEXT(""), FurthestRoomReached);
	static const TCHAR* Responses[] = { TEXT("pending"), TEXT("accepted"), TEXT("declined"), TEXT("expired") };
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AFPSRLPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		if (!PS)
		{
			continue;
		}
		const FCatchUpOffer* Offer = CatchUpOffers.Find(PS);
		const APawn* Pawn = It->Get()->GetPawn();
		const FString Where = Pawn ? FString::Printf(TEXT(" at %s"), *Pawn->GetActorLocation().ToCompactString()) : FString();
		const FString OfferText = Offer ? FString::Printf(TEXT("room %d %s"), Offer->RoomIndex, Responses[(int32)Offer->Response]) : FString(TEXT("none"));
		Text += FString::Printf(TEXT(" | %s: room %d%s%s, %s, offer %s"), *PS->GetPlayerName(), PS->ExpeditionRoomIndex,
			PS->ExpeditionRoomIndex < ActiveEncounterIndex ? TEXT(" (behind)") : TEXT(""), *Where,
			!Pawn ? TEXT("no pawn") : UFPSRLHealthComponent::IsPawnUp(Pawn) ? TEXT("up") : TEXT("down"), *OfferText);
	}
	return Text;
}
