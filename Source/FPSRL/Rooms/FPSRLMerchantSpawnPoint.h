// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FPSRLMerchantSpawnPoint.generated.h"

class UArrowComponent;
class UTextRenderComponent;

/**
 * Where the Merchant will stand (Depth 4's Preparation room). There is no shop yet: this only reserves the spot and
 * shows a placeholder sign, so the future Merchant can be spawned here without changing the room or the Depth.
 * Set MerchantClass once a Merchant exists; the server spawns it here at BeginPlay.
 */
UCLASS()
class FPSRL_API AFPSRLMerchantSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AFPSRLMerchantSpawnPoint();

	/** The future Merchant. Empty = placeholder sign only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	TSoftClassPtr<AActor> MerchantClass;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<UArrowComponent> Arrow;

	/** "MERCHANT (coming soon)" while there is no Merchant. */
	UPROPERTY(VisibleAnywhere, Category = "Merchant")
	TObjectPtr<UTextRenderComponent> Sign;
};
