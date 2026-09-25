// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/FPSRLRoomDefinition.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Rooms/FPSRLRoomConnector.h"
#include "FPSRL.h"

void UFPSRLRoomDefinition::BakeExitFromLevel()
{
#if WITH_EDITOR
	UWorld* World = Level.LoadSynchronous();
	if (!World || !World->PersistentLevel)
	{
		UE_LOG(LogFPSRL, Error, TEXT("[Room %s] Bake Exit: no Level set, or it failed to load"), *GetName());
		return;
	}

	int32 Found = 0;
	for (const AActor* Actor : World->PersistentLevel->Actors)
	{
		if (const AFPSRLRoomConnector* Connector = Cast<AFPSRLRoomConnector>(Actor))
		{
			if (Found++ == 0)
			{
				Modify();
				ExitTransform = Connector->GetActorTransform();
				ExitTransform.SetScale3D(FVector::OneVector);
			}
		}
	}

	if (Found == 0)
	{
		UE_LOG(LogFPSRL, Error, TEXT("[Room %s] Bake Exit: %s has no AFPSRLRoomConnector"), *GetName(), *World->GetName());
	}
	else
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Room %s] exit baked: %s%s"), *GetName(), *ExitTransform.ToHumanReadableString(),
			Found > 1 ? TEXT(" (more than one connector; used the first)") : TEXT(""));
	}
#endif
}
