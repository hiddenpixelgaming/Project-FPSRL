// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLTeamHUDWidget.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Attributes/FPSRLHealthSet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

#define LOCTEXT_NAMESPACE "FPSRLTeamHUD"

namespace FPSRLTeamHUD
{
	static TAutoConsoleVariable<bool> CVarLogHealth(TEXT("fpsrl.TeamHUD.LogHealth"), false,
		TEXT("Log every teammate health change the teammate HUD shows (tests)."));
}

// --- Entry ---------------------------------------------------------------------------------------------------------

void UFPSRLTeammateEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// Name (and DOWN / DEAD) over a slim health bar, on a dark backing.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	Panel->SetPadding(FMargin(10.f, 6.f));
	WidgetTree->RootWidget = Panel;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	auto MakeText = [this](const TCHAR* Name, int32 Size)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		return Text;
	};

	UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NameRow"));
	Column->AddChildToVerticalBox(NameRow);
	PlayerName = MakeText(TEXT("PlayerName"), 14);
	PlayerName->SetClipping(EWidgetClipping::ClipToBounds);
	if (UHorizontalBoxSlot* NameSlot = NameRow->AddChildToHorizontalBox(PlayerName))
	{
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	StatusText = MakeText(TEXT("StatusText"), 12);
	StatusText->SetColorAndOpacity(FSlateColor(DownedHealthColor));
	if (UHorizontalBoxSlot* StatusSlot = NameRow->AddChildToHorizontalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		StatusSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HealthSize"));
	BarSize->SetWidthOverride(240.f);
	BarSize->SetHeightOverride(10.f);
	HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	HealthBar->SetPercent(0.f);
	BarSize->SetContent(HealthBar);
	if (UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(BarSize))
	{
		BarSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
}

void UFPSRLTeammateEntryWidget::BeginDestroy()
{
	// Not on NativeDestruct: reordering the list or a travel takes the entry off the screen and back.
	Unbind();
	Super::BeginDestroy();
}

void UFPSRLTeammateEntryWidget::BindPlayer(AFPSRLPlayerState* InPlayer)
{
	if (InPlayer == Player.Get() && (InPlayer == nullptr || BoundASC.IsValid()))
	{
		return;
	}
	Unbind();
	Player = InPlayer;
	UAbilitySystemComponent* ASC = InPlayer ? InPlayer->GetAbilitySystemComponent() : nullptr;
	if (ASC)
	{
		BoundASC = ASC;
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
		MaxHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::HandleMaxHealthChanged);
		DownedHandle = ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Downed, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::HandleStatusTagChanged);
		DeadHandle = ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Dead, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::HandleStatusTagChanged);
		// Current values (they may still be arriving: the delegates bring the rest).
		bool bFound = false;
		const float CurrentHealth = ASC->GetGameplayAttributeValue(UFPSRLHealthSet::GetHealthAttribute(), bFound);
		if (bFound)
		{
			Health = CurrentHealth;
			MaxHealth = ASC->GetGameplayAttributeValue(UFPSRLHealthSet::GetMaxHealthAttribute(), bFound);
		}
		bDowned = ASC->HasMatchingGameplayTag(FPSRLGameplayTags::Status_Downed);
		bDead = ASC->HasMatchingGameplayTag(FPSRLGameplayTags::Status_Dead);
	}
	RefreshName();
	RefreshHealth();
	RefreshStatus();
}

void UFPSRLTeammateEntryWidget::Unbind()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetHealthAttribute()).Remove(HealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UFPSRLHealthSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Downed, EGameplayTagEventType::NewOrRemoved).Remove(DownedHandle);
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadHandle);
	}
	BoundASC.Reset();
	HealthHandle.Reset();
	MaxHealthHandle.Reset();
	DownedHandle.Reset();
	DeadHandle.Reset();
	Player.Reset();
}

void UFPSRLTeammateEntryWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	Health = Data.NewValue;
	RefreshHealth();
	RefreshStatus();
}

void UFPSRLTeammateEntryWidget::HandleMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	MaxHealth = Data.NewValue;
	RefreshHealth();
}

void UFPSRLTeammateEntryWidget::HandleStatusTagChanged(const FGameplayTag Tag, int32 Count)
{
	if (Tag == FPSRLGameplayTags::Status_Downed)
	{
		bDowned = Count > 0;
	}
	else if (Tag == FPSRLGameplayTags::Status_Dead)
	{
		bDead = Count > 0;
	}
	RefreshHealth();
	RefreshStatus();
}

void UFPSRLTeammateEntryWidget::RefreshHealth()
{
	if (HealthBar)
	{
		HealthBar->SetPercent(MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f);
		HealthBar->SetFillColorAndOpacity(bDowned ? DownedHealthColor : HealthColor);
	}
	if (FPSRLTeamHUD::CVarLogHealth.GetValueOnGameThread())
	{
		const APlayerController* Owner = GetOwningPlayer();
		UE_LOG(LogFPSRL, Log, TEXT("[TeamHUD] %s sees %s"), Owner && Owner->PlayerState ? *Owner->PlayerState->GetPlayerName() : TEXT("?"), *DescribeForTest());
	}
}

void UFPSRLTeammateEntryWidget::RefreshName()
{
	if (PlayerName && Player.IsValid())
	{
		const FString Name = Player->GetPlayerName();
		PlayerName->SetText(FText::FromString(bMuted ? FString::Printf(TEXT("%s  (muted)"), *Name) : Name));
	}
	RefreshNameStyle();
}

void UFPSRLTeammateEntryWidget::RefreshNameStyle()
{
	if (PlayerName)
	{
		PlayerName->SetColorAndOpacity(FSlateColor(bSpeaking ? SpeakingNameColor : NameColor));
	}
}

void UFPSRLTeammateEntryWidget::RefreshStatus()
{
	if (StatusText)
	{
		const bool bShowDead = bDead || (!bDowned && MaxHealth > 0.f && Health <= 0.f);
		StatusText->SetText(bDowned ? LOCTEXT("Downed", "DOWN") : bShowDead ? LOCTEXT("Dead", "DEAD") : FText::GetEmpty());
	}
}

void UFPSRLTeammateEntryWidget::SetSpeaking(bool bInSpeaking)
{
	if (bSpeaking != bInSpeaking)
	{
		bSpeaking = bInSpeaking;
		RefreshNameStyle();
	}
}

void UFPSRLTeammateEntryWidget::SetMuted(bool bInMuted)
{
	if (bMuted != bInMuted)
	{
		bMuted = bInMuted;
		RefreshName();
	}
}

FString UFPSRLTeammateEntryWidget::DescribeForTest() const
{
	const FString Name = PlayerName ? PlayerName->GetText().ToString() : (Player.IsValid() ? Player->GetPlayerName() : TEXT("?"));
	FString Out = FString::Printf(TEXT("%s %d/%d"), *Name, FMath::CeilToInt(Health), FMath::CeilToInt(MaxHealth));
	if (bSpeaking)
	{
		Out += TEXT(" [speaking]");
	}
	if (StatusText && !StatusText->GetText().IsEmpty())
	{
		Out += FString::Printf(TEXT(" [%s]"), *StatusText->GetText().ToString());
	}
	if (!Player.IsValid())
	{
		Out += TEXT(" [detached]");
	}
	return Out;
}

// --- Team HUD ------------------------------------------------------------------------------------------------------

double UFPSRLTeamHUDWidget::LastTravelStart = -1.0e9;

void UFPSRLTeamHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);	// never takes input
	BeginHandle = AFPSRLPlayerState::OnPlayerStateBegin.AddUObject(this, &ThisClass::HandlePlayerStateBegin);
	EndHandle = AFPSRLPlayerState::OnPlayerStateEnd.AddUObject(this, &ThisClass::HandlePlayerStateEnd);
	ChangedHandle = AFPSRLPlayerState::OnPlayerStateChanged.AddUObject(this, &ThisClass::HandlePlayerStateChanged);
	TravelHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &ThisClass::HandleSeamlessTravelStart);

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	// Top left: a column of teammate entries.
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	WidgetTree->RootWidget = Canvas;
	EntryList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EntryList"));
	if (UCanvasPanelSlot* ListSlot = Canvas->AddChildToCanvas(EntryList))
	{
		ListSlot->SetAnchors(FAnchors(0.f, 0.f));
		ListSlot->SetAlignment(FVector2D(0.f, 0.f));
		ListSlot->SetPosition(FVector2D(40.f, 40.f));
		ListSlot->SetAutoSize(true);
	}
}

void UFPSRLTeamHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Resync();	// on screen again (first time, or after a travel): pick up the PlayerStates already here
}

void UFPSRLTeamHUDWidget::NativeDestruct()
{
	// Taken off the screen (a travel does that too): keep the entries and the subscriptions, the controller puts this
	// widget back. Only the prune timer belongs to the old world.
	if (UWorld* World = PruneWorld.Get())
	{
		World->GetTimerManager().ClearTimer(PruneTimer);
	}
	PruneWorld.Reset();
	Super::NativeDestruct();
}

void UFPSRLTeamHUDWidget::BeginDestroy()
{
	AFPSRLPlayerState::OnPlayerStateBegin.Remove(BeginHandle);
	AFPSRLPlayerState::OnPlayerStateEnd.Remove(EndHandle);
	AFPSRLPlayerState::OnPlayerStateChanged.Remove(ChangedHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(TravelHandle);
	Super::BeginDestroy();
}

bool UFPSRLTeamHUDWidget::IsOwnerGone() const
{
	const APlayerController* Owner = GetOwningPlayer();
	return !Owner || Owner->IsActorBeingDestroyed() || !Owner->IsLocalController();
}

void UFPSRLTeamHUDWidget::HandleSeamlessTravelStart(UWorld* World, const FString& LevelName)
{
	LastTravelStart = FPlatformTime::Seconds();
}

bool UFPSRLTeamHUDWidget::IsTeammate(const AFPSRLPlayerState* PlayerState) const
{
	const APlayerController* Owner = GetOwningPlayer();
	// Bots (AI characters with a PlayerState) are not teammates. Nor is a state that has no name yet: on the host a new
	// state exists for a moment before the game mode (join) or the travel copy gives it the player's id and name; the
	// name change event follows.
	if (!PlayerState || !Owner || PlayerState->GetWorld() != Owner->GetWorld() || PlayerState->IsInactive() || PlayerState->IsABot()
		|| PlayerState->GetPlayerName().IsEmpty()
		|| PlayerState->IsActorBeingDestroyed() || !PlayerState->HasActorBegunPlay())
	{
		return false;
	}
	// Never the local player (their health is the personal HUD's). Three checks, so a client excludes itself even
	// before its controller's PlayerState pointer has replicated: the controller's PlayerState, the replicated owner
	// (only our own PlayerState is owned by a controller on this machine), and the local player's online id.
	if (PlayerState == Owner->PlayerState || PlayerState->GetOwner() == Owner)
	{
		return false;
	}
	// Owned by any controller of this machine's player (a travel spawns the new controller and its PlayerState before
	// the old controller is gone). On a client other players' PlayerStates have no owner; on the host theirs are
	// owned by remote controllers.
	if (const APlayerController* OwnerController = Cast<APlayerController>(PlayerState->GetOwner()); OwnerController && OwnerController->IsLocalController())
	{
		return false;
	}
	const ULocalPlayer* LocalPlayer = Owner->GetLocalPlayer();
	const FUniqueNetIdRepl LocalId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	return !(LocalId.IsValid() && PlayerState->GetUniqueId().IsValid() && LocalId == PlayerState->GetUniqueId());
}

void UFPSRLTeamHUDWidget::Resync()
{
	const APlayerController* Owner = GetOwningPlayer();
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (GameState)
	{
		for (APlayerState* Other : GameState->PlayerArray)
		{
			if (AFPSRLPlayerState* PlayerState = Cast<AFPSRLPlayerState>(Other))
			{
				HandlePlayerStateChanged(PlayerState);
			}
		}
	}
	// Entries for the local player (it was not known yet) or for states no longer here are dropped / detached.
	TArray<int32> Drop;
	for (const TPair<int32, TObjectPtr<UFPSRLTeammateEntryWidget>>& Pair : Entries)
	{
		AFPSRLPlayerState* Bound = Pair.Value ? Pair.Value->GetPlayer() : nullptr;
		if (Bound && Bound->GetWorld() == World && !IsTeammate(Bound))
		{
			Drop.Add(Pair.Key);
		}
	}
	for (int32 PlayerId : Drop)
	{
		RemoveEntry(PlayerId, TEXT("not a teammate"));
	}
	if (World)
	{
		SchedulePrune(const_cast<UWorld*>(World));
	}
}

void UFPSRLTeamHUDWidget::HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState)
{
	HandlePlayerStateChanged(PlayerState);
	if (!IsOwnerGone() && PlayerState)
	{
		SchedulePrune(PlayerState->GetWorld());	// after a travel: entries still detached go if their player never comes
	}
}

void UFPSRLTeamHUDWidget::HandlePlayerStateChanged(AFPSRLPlayerState* PlayerState)
{
	const APlayerController* Owner = GetOwningPlayer();
	if (!PlayerState || IsOwnerGone() || PlayerState->GetWorld() != Owner->GetWorld())
	{
		return;	// another world (PIE instance, the world being left)
	}
	if (IsTeammate(PlayerState))
	{
		AddOrRebind(PlayerState);
	}
	else if (TObjectPtr<UFPSRLTeammateEntryWidget>* Entry = Entries.Find(PlayerState->GetPlayerId()))
	{
		if (*Entry && (*Entry)->GetPlayer() == PlayerState)
		{
			RemoveEntry(PlayerState->GetPlayerId(), PlayerState->IsInactive() ? TEXT("inactive") : TEXT("local player"));
		}
	}
}

void UFPSRLTeamHUDWidget::HandlePlayerStateEnd(AFPSRLPlayerState* PlayerState, EEndPlayReason::Type Reason)
{
	if (IsOwnerGone())
	{
		return;
	}
	TObjectPtr<UFPSRLTeammateEntryWidget>* Entry = PlayerState ? Entries.Find(PlayerState->GetPlayerId()) : nullptr;
	if (!Entry || !*Entry || (*Entry)->GetPlayer() != PlayerState)
	{
		return;
	}
	// A Depth travel replaces every PlayerState: the old ones go with the old world (LevelTransition) or are destroyed
	// just after the travel finished. Either way the teammate's next PlayerState re-binds the entry (or the prune
	// removes it if they never arrive). A destroy at any other time is the player leaving: disconnect, leave, lost.
	const bool bTravel = Reason != EEndPlayReason::Destroyed || FPlatformTime::Seconds() - LastTravelStart < TravelGrace;
	if (!bTravel)
	{
		RemoveEntry(PlayerState->GetPlayerId(), TEXT("left the session"));
		return;
	}
	(*Entry)->BindPlayer(nullptr);
	if (const APlayerController* Owner = GetOwningPlayer())
	{
		SchedulePrune(Owner->GetWorld());
	}
}

void UFPSRLTeamHUDWidget::AddOrRebind(AFPSRLPlayerState* PlayerState)
{
	const int32 PlayerId = PlayerState->GetPlayerId();
	// On the server a joining player's state begins play before the game mode gives it its id and name: the entry made
	// then moves to the real id when the name change arrives.
	for (const TPair<int32, TObjectPtr<UFPSRLTeammateEntryWidget>>& Pair : Entries)
	{
		if (Pair.Key != PlayerId && Pair.Value && Pair.Value->GetPlayer() == PlayerState)
		{
			TObjectPtr<UFPSRLTeammateEntryWidget> Moved = Pair.Value;
			Entries.Remove(Pair.Key);
			Entries.Add(PlayerId, Moved);
			RebuildOrder();
			break;
		}
	}
	if (TObjectPtr<UFPSRLTeammateEntryWidget>* Existing = Entries.Find(PlayerId))
	{
		if (*Existing)
		{
			const bool bWasDetached = !(*Existing)->GetPlayer();
			(*Existing)->BindPlayer(PlayerState);
			if (bWasDetached)
			{
				LogState(*FString::Printf(TEXT("%s back after the travel"), *PlayerState->GetPlayerName()));
			}
			(*Existing)->RefreshName();
		}
		return;
	}
	APlayerController* Owner = GetOwningPlayer();
	UFPSRLTeammateEntryWidget* Entry = Owner ? CreateWidget<UFPSRLTeammateEntryWidget>(Owner, EntryClass ? EntryClass : TSubclassOf<UFPSRLTeammateEntryWidget>(UFPSRLTeammateEntryWidget::StaticClass())) : nullptr;
	if (!Entry)
	{
		return;
	}
	Entries.Add(PlayerId, Entry);
	Entry->BindPlayer(PlayerState);
	Entry->SetSpeaking(false);	// voice starts inactive
	RebuildOrder();
	LogState(*FString::Printf(TEXT("%s joined (id %d, owner %s)"), *PlayerState->GetPlayerName(), PlayerId, PlayerState->GetOwner() ? *PlayerState->GetOwner()->GetClass()->GetName() : TEXT("none")));
}

void UFPSRLTeamHUDWidget::RemoveEntry(int32 PlayerId, const TCHAR* Why)
{
	TObjectPtr<UFPSRLTeammateEntryWidget> Entry;
	if (!Entries.RemoveAndCopyValue(PlayerId, Entry))
	{
		return;
	}
	const FString Name = Entry && Entry->GetPlayer() ? Entry->GetPlayer()->GetPlayerName() : FString::Printf(TEXT("player %d"), PlayerId);
	if (Entry)
	{
		Entry->BindPlayer(nullptr);
		Entry->RemoveFromParent();
	}
	RebuildOrder();
	LogState(*FString::Printf(TEXT("%s removed (%s)"), *Name, Why));
}

void UFPSRLTeamHUDWidget::RebuildOrder()
{
	if (!EntryList)
	{
		return;
	}
	// Stable order: PlayerId (assigned as players join, kept across travel), so health or voice never reorders it.
	TArray<int32> Ids;
	Entries.GetKeys(Ids);
	Ids.Sort();
	EntryList->ClearChildren();
	for (int32 PlayerId : Ids)
	{
		if (UVerticalBoxSlot* EntrySlot = EntryList->AddChildToVerticalBox(Entries[PlayerId]))
		{
			EntrySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}
	}
	EntryList->SetVisibility(Ids.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);	// solo: nothing
}

void UFPSRLTeamHUDWidget::SchedulePrune(UWorld* World)
{
	bool bAnyDetached = false;
	for (const TPair<int32, TObjectPtr<UFPSRLTeammateEntryWidget>>& Pair : Entries)
	{
		bAnyDetached |= Pair.Value && !Pair.Value->GetPlayer();
	}
	if (!bAnyDetached || !World || (PruneWorld.Get() == World && World->GetTimerManager().IsTimerActive(PruneTimer)))
	{
		return;
	}
	PruneWorld = World;
	World->GetTimerManager().SetTimer(PruneTimer, this, &ThisClass::PruneDetached, PruneDelay, false);
}

void UFPSRLTeamHUDWidget::PruneDetached()
{
	TArray<int32> Gone;
	for (const TPair<int32, TObjectPtr<UFPSRLTeammateEntryWidget>>& Pair : Entries)
	{
		if (!Pair.Value || !Pair.Value->GetPlayer())
		{
			Gone.Add(Pair.Key);
		}
	}
	for (int32 PlayerId : Gone)
	{
		RemoveEntry(PlayerId, TEXT("did not arrive after the travel"));
	}
}

void UFPSRLTeamHUDWidget::SetSpeakingState(int32 PlayerId, bool bSpeaking)
{
	if (TObjectPtr<UFPSRLTeammateEntryWidget>* Entry = Entries.Find(PlayerId); Entry && *Entry)
	{
		(*Entry)->SetSpeaking(bSpeaking);
	}
}

void UFPSRLTeamHUDWidget::SetMutedState(int32 PlayerId, bool bMuted)
{
	if (TObjectPtr<UFPSRLTeammateEntryWidget>* Entry = Entries.Find(PlayerId); Entry && *Entry)
	{
		(*Entry)->SetMuted(bMuted);
	}
}

FString UFPSRLTeamHUDWidget::DescribeForTest() const
{
	TArray<int32> Ids;
	Entries.GetKeys(Ids);
	Ids.Sort();
	TArray<FString> Parts;
	for (int32 PlayerId : Ids)
	{
		Parts.Add(Entries[PlayerId] ? Entries[PlayerId]->DescribeForTest() : TEXT("?"));
	}
	return FString::Printf(TEXT("%d teammate(s)%s%s"), Ids.Num(), Ids.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Parts, TEXT(" | ")));
}

void UFPSRLTeamHUDWidget::LogState(const TCHAR* Why) const
{
	const APlayerController* Owner = GetOwningPlayer();
	UE_LOG(LogFPSRL, Log, TEXT("[TeamHUD] %s (%s): %s -> %s"), Owner && Owner->PlayerState ? *Owner->PlayerState->GetPlayerName() : TEXT("(local)"), *GetName(), Why, *DescribeForTest());
}

#undef LOCTEXT_NAMESPACE

// --- Session holder ------------------------------------------------------------------------------------------------

UFPSRLTeamHUDWidget* UFPSRLTeamHUDSubsystem::GetOrCreateTeamHUD(APlayerController* Owner, TSubclassOf<UFPSRLTeamHUDWidget> Class)
{
	if (!Owner)
	{
		return TeamHUD;
	}
	if (!TeamHUD)
	{
		TeamHUD = CreateWidget<UFPSRLTeamHUDWidget>(Owner, Class ? Class : TSubclassOf<UFPSRLTeamHUDWidget>(UFPSRLTeamHUDWidget::StaticClass()));
	}
	else if (TeamHUD->GetOwningPlayer() != Owner)
	{
		TeamHUD->RemoveFromParent();
		TeamHUD->SetOwningPlayer(Owner);	// a travel replaced the controller: the same HUD carries on
	}
	return TeamHUD;
}

void UFPSRLTeamHUDSubsystem::ResetTeamHUD()
{
	if (TeamHUD)
	{
		TeamHUD->RemoveFromParent();
		TeamHUD = nullptr;
	}
}
