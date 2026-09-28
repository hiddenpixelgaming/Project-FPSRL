// Fill out your copyright notice in the Description page of Project Settings.

#include "Rooms/FPSRLMerchantSpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"

AFPSRLMerchantSpawnPoint::AFPSRLMerchantSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowColor = FColor(80, 220, 255);
	Arrow->SetHiddenInGame(true);

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(40.f);
	Sign->SetTextRenderColor(FColor(80, 220, 255));
	Sign->SetText(NSLOCTEXT("FPSRL", "MerchantPlaceholder", "MERCHANT\n(coming soon)"));
}

void AFPSRLMerchantSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	// Once a Merchant exists, it takes this spot and the placeholder sign goes away.
	UClass* Class = MerchantClass.LoadSynchronous();
	if (Class && HasAuthority())
	{
		GetWorld()->SpawnActor<AActor>(Class, GetActorTransform());
	}
	Sign->SetVisibility(Class == nullptr);
}
