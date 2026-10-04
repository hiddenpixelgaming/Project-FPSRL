// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLNameplateSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Social/FPSRLVoiceSubsystem.h"
#include "UI/FPSRLTeamHUDWidget.h"
#include "FPSRL.h"

namespace FPSRLNameplate
{
	static const FName ComponentName(TEXT("TeammateNameplate"));
	static const FVector2D DrawSize(200.f, 48.f);
	static constexpr float AboveHead = 15.f;	// cm over the top of the capsule
}

bool UFPSRLNameplateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && !IsRunningDedicatedServer();
}

void UFPSRLNameplateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BeginHandle = AFPSRLPlayerState::OnPlayerStateBegin.AddUObject(this, &ThisClass::HandlePlayerStateBegin);
	for (TActorIterator<AFPSRLPlayerState> It(&InWorld); It; ++It)
	{
		HandlePlayerStateBegin(*It);
	}
}

void UFPSRLNameplateSubsystem::Deinitialize()
{
	AFPSRLPlayerState::OnPlayerStateBegin.Remove(BeginHandle);
	if (UWorld* World = GetWorld(); World && bVoiceBound)
	{
		if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr))
		{
			Voice->OnSpeakingChanged.Remove(SpeakingHandle);
		}
	}
	Super::Deinitialize();
}

int32 UFPSRLNameplateSubsystem::GetNameplateCount() const
{
	int32 Count = 0;
	for (const TPair<int32, TWeakObjectPtr<UWidgetComponent>>& Plate : Plates)
	{
		Count += Plate.Value.IsValid() ? 1 : 0;
	}
	return Count;
}

void UFPSRLNameplateSubsystem::HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState)
{
	if (!PlayerState || PlayerState->GetWorld() != GetWorld() || PlayerState->IsABot())
	{
		return;
	}
	PlayerState->OnPawnSet.AddUniqueDynamic(this, &ThisClass::HandlePawnSet);
	AddNameplate(PlayerState, PlayerState->GetPawn());
}

void UFPSRLNameplateSubsystem::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	AddNameplate(Cast<AFPSRLPlayerState>(Player), NewPawn);
}

void UFPSRLNameplateSubsystem::AddNameplate(AFPSRLPlayerState* PlayerState, APawn* Pawn)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	// This machine's own player: its pawn may not be possessed yet when it arrives, so its player state's owner counts too.
	const APlayerController* Owner = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	const bool bLocal = (Owner && Owner->IsLocalController()) || (Character && Character->IsLocallyControlled());
	if (!PlayerState || !Character || bLocal || Character->FindComponentByTag<UWidgetComponent>(FPSRLNameplate::ComponentName))
	{
		return;	// nobody to label, this machine's own player, or already labelled
	}
	BindVoice();
	UWidgetComponent* Plate = NewObject<UWidgetComponent>(Character, FPSRLNameplate::ComponentName);
	Plate->ComponentTags.Add(FPSRLNameplate::ComponentName);
	Plate->SetWidgetSpace(EWidgetSpace::Screen);	// always facing the camera, over walls: spot a teammate who needs help
	Plate->SetDrawSize(FPSRLNameplate::DrawSize);
	Plate->SetDrawAtDesiredSize(true);
	Plate->SetPivot(FVector2D(0.5f, 1.f));
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetWidgetClass(UFPSRLTeammateEntryWidget::StaticClass());
	Plate->SetupAttachment(Character->GetRootComponent());
	Plate->SetRelativeLocation(FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + FPSRLNameplate::AboveHead));
	Plate->RegisterComponent();
	Character->AddInstanceComponent(Plate);
	Plate->InitWidget();
	if (UFPSRLTeammateEntryWidget* Entry = Cast<UFPSRLTeammateEntryWidget>(Plate->GetUserWidgetObject()))
	{
		Entry->BindPlayer(PlayerState);
		const UWorld* World = GetWorld();
		const UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr);
		Entry->SetSpeaking(Voice && Voice->IsSpeaking(PlayerState->GetPlayerId()));
	}
	Plates.Add(PlayerState->GetPlayerId(), Plate);
	UE_LOG(LogFPSRL, Log, TEXT("[Nameplate] %s gets a nameplate (%s)"), *PlayerState->GetPlayerName(), *Character->GetName());
}

void UFPSRLNameplateSubsystem::BindVoice()
{
	UWorld* World = GetWorld();
	if (bVoiceBound || !World)
	{
		return;
	}
	if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr))
	{
		SpeakingHandle = Voice->OnSpeakingChanged.AddUObject(this, &ThisClass::HandleSpeakingChanged);
		bVoiceBound = true;
	}
}

void UFPSRLNameplateSubsystem::HandleSpeakingChanged(int32 PlayerId, bool bSpeaking)
{
	const UWidgetComponent* Plate = Plates.FindRef(PlayerId).Get();
	if (UFPSRLTeammateEntryWidget* Entry = Plate ? Cast<UFPSRLTeammateEntryWidget>(Plate->GetUserWidgetObject()) : nullptr)
	{
		Entry->SetSpeaking(bSpeaking);
	}
}
