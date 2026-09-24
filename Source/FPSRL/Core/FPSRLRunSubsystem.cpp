// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLRunSubsystem.h"
#include "Data/FPSRLRunDefinition.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/World.h"
#include "FPSRL.h"

bool UFPSRLRunSubsystem::StartRun(UWorld* World)
{
	const UFPSRLRunDefinition* NewRun = UFPSRLRunSettings::Get().RunDefinition.LoadSynchronous();
	const UFPSRLDepthDefinition* First = NewRun ? NewRun->GetDepth(0) : nullptr;
	if (!World || !First)
	{
		return false;
	}

	Run = NewRun;
	DepthIndex = 0;
	bRunActive = true;
	UE_LOG(LogFPSRL, Log, TEXT("[Run] Started %s (%d Depths)"), *GetNameSafe(Run), Run->GetNumDepths());
	return TravelTo(World, First->Map);
}

void UFPSRLRunSubsystem::AdvanceRun(UWorld* World)
{
	const EPortalDestination Destination = GetNextDestination();
	if (Destination == EPortalDestination::RunComplete)
	{
		EndRun(World, true);
		return;
	}

	++DepthIndex;
	const UFPSRLDepthDefinition* Next = GetCurrentDepth();
	UE_LOG(LogFPSRL, Log, TEXT("[Run] Advancing to Depth %d (%s, %s)"), GetDepthNumber(), *GetNameSafe(Next),
		*UEnum::GetValueAsString(Destination));
	if (!Next || !TravelTo(World, Next->Map))
	{
		UE_LOG(LogFPSRL, Error, TEXT("[Run] Depth %d has no map; ending the run"), GetDepthNumber());
		EndRun(World, false);
	}
}

void UFPSRLRunSubsystem::EndRun(UWorld* World, bool bVictory)
{
	UE_LOG(LogFPSRL, Log, TEXT("[Run] Ended (%s) at Depth %d"), bVictory ? TEXT("complete") : TEXT("failed"), GetDepthNumber());
	bRunActive = false;
	Run = nullptr;
	DepthIndex = 0;

	if (!TravelTo(World, UFPSRLRunSettings::Get().LobbyMap))
	{
		UE_LOG(LogFPSRL, Error, TEXT("[Run] No Lobby Map set in Project Settings > FPSRL Run"));
	}
}

int32 UFPSRLRunSubsystem::GetAreaNumber() const
{
	int32 AreaIndex = INDEX_NONE;
	return (bRunActive && Run && Run->GetDepth(DepthIndex, &AreaIndex)) ? AreaIndex + 1 : 0;
}

const UFPSRLDepthDefinition* UFPSRLRunSubsystem::GetCurrentDepth() const
{
	return (bRunActive && Run) ? Run->GetDepth(DepthIndex) : nullptr;
}

EPortalDestination UFPSRLRunSubsystem::GetNextDestination() const
{
	int32 CurrentArea = INDEX_NONE;
	int32 NextArea = INDEX_NONE;
	const UFPSRLDepthDefinition* Current = (bRunActive && Run) ? Run->GetDepth(DepthIndex, &CurrentArea) : nullptr;
	const UFPSRLDepthDefinition* Next = Current ? Run->GetDepth(DepthIndex + 1, &NextArea) : nullptr;
	if (!Next)
	{
		return EPortalDestination::RunComplete;
	}
	if (Next->DepthType == EDepthType::FinalBoss)
	{
		return EPortalDestination::FinalBoss;
	}
	return NextArea != CurrentArea ? EPortalDestination::NextArea : EPortalDestination::NextDepth;
}

bool UFPSRLRunSubsystem::TravelTo(UWorld* World, const TSoftObjectPtr<UWorld>& Map) const
{
	if (!World || Map.IsNull())
	{
		return false;
	}
	// Seamless travel (the GameMode's bUseSeamlessTravel) keeps everyone connected and carries each PlayerState's
	// build to the next Depth via CopyProperties.
	return World->ServerTravel(Map.GetLongPackageName(), false);
}
