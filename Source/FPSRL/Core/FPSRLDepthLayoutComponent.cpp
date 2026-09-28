// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLDepthLayoutComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerController.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Data/FPSRLRunDefinition.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLRewardSpawnPoint.h"
#include "Rooms/FPSRLRoomConnector.h"
#include "TimerManager.h"
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
}

bool UFPSRLDepthLayoutComponent::IsEncounterRoom(ERoomType Type)
{
	return Type == ERoomType::Combat || Type == ERoomType::Elite || Type == ERoomType::Boss;
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
		Add(PickRoom(Depth.CombatRooms, Used, AnyRoom));
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

	if (Depth.bHasElite)
	{
		Add(PickRoom(Depth.EliteRooms, Used, AnyRoom));	// an extra encounter after the last combat room
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
	UpdateWindow();
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
	AFPSRLPlayerController* LocalPC = Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController());
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
		else if (LocalPC)
		{
			LocalPC->ServerReportRoomShown(Entry.Key);	// the server opens gates only once every machine has the room
		}
	}
	if (!bServer)
	{
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
	// A report can be lost if it raced this client's controller; re-send anything loaded here but not counted yet.
	if (AFPSRLPlayerController* LocalPC = Cast<AFPSRLPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		for (const int32 Index : ShownHere)
		{
			if (Index > ReadyThrough)
			{
				LocalPC->ServerReportRoomShown(Index);
			}
		}
	}
	OnReadinessChanged.Broadcast();
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
	if (Placement.Reward != EFPSRLTraversalReward::None)
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
	SetRoomState(FindPlacementIndex(RoomActor), EFPSRLRoomState::Combat);
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
	UpdateWindow();	// start loading what comes next
}

void UFPSRLDepthLayoutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OccupancyTimer);
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
