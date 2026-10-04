// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FPSRLNameplateSubsystem.generated.h"

class AFPSRLPlayerState;
class APawn;
class APlayerState;
class UWidgetComponent;

/**
 * Teammates' nameplates: above every other player's head, their name and health bar (the teammate HUD's entry widget:
 * DOWN / DEAD, yellow name while speaking), drawn on screen so a teammate who needs help can be spotted through walls.
 * Never on this machine's own player. Per machine (cosmetic), event driven: a player state arriving or getting a pawn
 * adds a plate to that pawn; the pawn going takes its plate with it.
 */
UCLASS()
class FPSRL_API UFPSRLNameplateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Plates on screen now (tests). */
	int32 GetNameplateCount() const;

private:
	void HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState);
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
	void AddNameplate(AFPSRLPlayerState* PlayerState, APawn* Pawn);
	void HandleSpeakingChanged(int32 PlayerId, bool bSpeaking);
	void BindVoice();

	TMap<int32, TWeakObjectPtr<UWidgetComponent>> Plates;
	FDelegateHandle BeginHandle;
	FDelegateHandle SpeakingHandle;
	bool bVoiceBound = false;
};
