// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLChatSubsystem.h"
#include "Social/FPSRLChatFilter.h"
#include "Social/FPSRLChatSettings.h"
#include "Components/FPSRLHealthComponent.h"
#include "Core/FPSRLDepthLayoutComponent.h"
#include "Core/FPSRLGameState.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "Data/FPSRLRoomDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

UFPSRLChatSubsystem* UFPSRLChatSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UFPSRLChatSubsystem>() : nullptr;
}

void UFPSRLChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddUObject(this, &ThisClass::HandlePostLogin);
	LogoutHandle = FGameModeEvents::GameModeLogoutEvent.AddUObject(this, &ThisClass::HandleLogout);
}

void UFPSRLChatSubsystem::Deinitialize()
{
	FGameModeEvents::GameModePostLoginEvent.Remove(PostLoginHandle);
	FGameModeEvents::GameModeLogoutEvent.Remove(LogoutHandle);
	Super::Deinitialize();
}

const UFPSRLChatFilter* UFPSRLChatSubsystem::GetFilter() const
{
	if (!Filter)
	{
		UClass* FilterClass = UFPSRLChatSettings::Get()->FilterClass.LoadSynchronous();
		Filter = NewObject<UFPSRLChatFilter>(const_cast<UFPSRLChatSubsystem*>(this), FilterClass ? FilterClass : UFPSRLChatFilter::StaticClass());
	}
	return Filter;
}

EFPSRLChatRejectReason UFPSRLChatSubsystem::CheckAllowed(const APlayerController* Sender) const
{
	const UFPSRLChatSettings* Settings = UFPSRLChatSettings::Get();
	const UWorld* World = Sender ? Sender->GetWorld() : nullptr;
	const APlayerState* State = Sender ? Sender->PlayerState : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;

	// A connected member of this session: its PlayerState is in the game state's player list.
	if (!State || !GameState || !GameState->PlayerArray.Contains(State) || (!Sender->IsLocalController() && !Sender->GetNetConnection()))
	{
		return EFPSRLChatRejectReason::NotInSession;
	}

	// Lobby or expedition: an expedition has a Depth layout.
	const AFPSRLGameState* FPSRLState = Cast<AFPSRLGameState>(GameState);
	const UFPSRLDepthLayoutComponent* Layout = FPSRLState ? FPSRLState->DepthLayout.Get() : nullptr;
	const bool bExpedition = Layout && Layout->HasLayout();
	if (bExpedition ? !Settings->bChatEnabledInExpedition : !Settings->bChatEnabledInLobby)
	{
		return EFPSRLChatRejectReason::Disabled;
	}

	// Downed / dead (a player without a living pawn counts as dead / spectating).
	const APawn* Pawn = Sender->GetPawn();
	const UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	const bool bDead = !Pawn || (Health && Health->IsDead());
	const bool bDowned = !bDead && Health && Health->IsDowned();
	if ((bDead && !Settings->bChatEnabledWhileDead) || (bDowned && !Settings->bChatEnabledWhileDowned))
	{
		return EFPSRLChatRejectReason::Disabled;
	}

	// In a traversal.
	if (bExpedition && !Settings->bChatEnabledDuringTraversal)
	{
		const AFPSRLPlayerState* PlayerState = Cast<AFPSRLPlayerState>(State);
		const int32 Room = PlayerState ? PlayerState->ExpeditionRoomIndex : INDEX_NONE;
		if (Layout->Placements.IsValidIndex(Room) && Layout->Placements[Room].Room && Layout->Placements[Room].Room->RoomType == ERoomType::Traversal)
		{
			return EFPSRLChatRejectReason::Disabled;
		}
	}
	return EFPSRLChatRejectReason::None;
}

EFPSRLChatRejectReason UFPSRLChatSubsystem::ServerSubmit(APlayerController* Sender, const FString& RawText)
{
	const UFPSRLChatSettings* Settings = UFPSRLChatSettings::Get();
	const APlayerState* State = Sender ? Sender->PlayerState : nullptr;
	const FString Who = State ? State->GetPlayerName() : FString(TEXT("?"));
	auto Reject = [this, State, &Who](EFPSRLChatRejectReason Reason)
	{
		LastRejectedPlayerId = State ? State->GetPlayerId() : INDEX_NONE;
		UE_LOG(LogFPSRL, Log, TEXT("[Chat] Message rejected - %s (from %s)"), *StaticEnum<EFPSRLChatRejectReason>()->GetNameStringByValue(static_cast<int64>(Reason)), *Who);
		return Reason;
	};

	if (!Sender || !Sender->HasAuthority())
	{
		return Reject(EFPSRLChatRejectReason::NotInSession);
	}
	const EFPSRLChatRejectReason Allowed = CheckAllowed(Sender);
	if (Allowed != EFPSRLChatRejectReason::None)
	{
		return Reject(Allowed);
	}

	// Length: an absurd payload is refused before any work; the real limit applies to the sanitised text.
	if (RawText.Len() > Settings->MaxMessageCharacters * 4)
	{
		return Reject(EFPSRLChatRejectReason::TooLong);
	}
	FString Text;
	EFPSRLChatRejectReason FilterReason = EFPSRLChatRejectReason::None;
	if (!GetFilter()->FilterMessage(RawText, Text, FilterReason))
	{
		return Reject(FilterReason != EFPSRLChatRejectReason::None ? FilterReason : EFPSRLChatRejectReason::Filtered);
	}
	if (Text.IsEmpty())
	{
		return Reject(EFPSRLChatRejectReason::Empty);
	}
	if (Text.Len() > Settings->MaxMessageCharacters)
	{
		return Reject(EFPSRLChatRejectReason::TooLong);
	}

	// Rate limit per player.
	const int32 PlayerId = State->GetPlayerId();
	const double Now = FPlatformTime::Seconds();
	if (const double* Last = LastAcceptedTime.Find(PlayerId); Last && Now - *Last < Settings->ChatMessageCooldown)
	{
		return Reject(EFPSRLChatRejectReason::RateLimited);
	}
	LastAcceptedTime.Add(PlayerId, Now);

	// Authoritative metadata: the server's own view of the sender, its own id and clock.
	FFPSRLChatMessage Message;
	Message.MessageId = NextMessageId++;
	Message.SenderPlayerId = PlayerId;
	Message.DisplayName = State->GetPlayerName();
	Message.Text = Text;
	Message.ServerTimestampTicks = FDateTime::UtcNow().GetTicks();
	ServerHistory.Add(Message);
	if (ServerHistory.Num() > Settings->MaxServerChatHistory)
	{
		ServerHistory.RemoveAt(0, ServerHistory.Num() - Settings->MaxServerChatHistory);
	}
	LastSender = Message.DisplayName;
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] Message accepted #%d from %s (%d chars)"), Message.MessageId, *Message.DisplayName, Message.Text.Len());

	// One message to every connected member.
	int32 Recipients = 0;
	for (FConstPlayerControllerIterator It = Sender->GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(It->Get()); PC && PC->PlayerState)
		{
			PC->ClientReceiveChatMessage(Message);
			++Recipients;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] Message broadcast #%d to %d player(s)"), Message.MessageId, Recipients);
	return EFPSRLChatRejectReason::None;
}

void UFPSRLChatSubsystem::ServerSendHistoryTo(APlayerController* Player) const
{
	AFPSRLPlayerController* PC = Cast<AFPSRLPlayerController>(Player);
	if (PC && !ServerHistory.IsEmpty())
	{
		PC->ClientReceiveChatHistory(ServerHistory);
	}
}

void UFPSRLChatSubsystem::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!GameMode || GameMode->GetGameInstance() != GetGameInstance() || !NewPlayer)
	{
		return;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] Player joined: %s (sending %d message(s) of history)"),
		NewPlayer->PlayerState ? *NewPlayer->PlayerState->GetPlayerName() : TEXT("?"), ServerHistory.Num());
	ServerSendHistoryTo(NewPlayer);
}

void UFPSRLChatSubsystem::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!GameMode || GameMode->GetGameInstance() != GetGameInstance() || !Exiting)
	{
		return;
	}
	// History stays; the player simply stops being a recipient (it is no longer in the controller list).
	if (const APlayerState* State = Exiting->PlayerState)
	{
		LastAcceptedTime.Remove(State->GetPlayerId());
		UE_LOG(LogFPSRL, Log, TEXT("[Chat] Player disconnected: %s"), *State->GetPlayerName());
	}
}

void UFPSRLChatSubsystem::ClientAddMessage(const FFPSRLChatMessage& Message)
{
	for (const FFPSRLChatMessage& Existing : ClientHistory)
	{
		if (Existing.MessageId == Message.MessageId)
		{
			return;
		}
	}
	FFPSRLChatMessage Arrived = Message;
	Arrived.LocalArrivalSeconds = FPlatformTime::Seconds();
	// Keep server order even if a history sync and a live message cross.
	int32 Index = ClientHistory.Num();
	while (Index > 0 && ClientHistory[Index - 1].MessageId > Message.MessageId)
	{
		--Index;
	}
	ClientHistory.Insert(Arrived, Index);
	const int32 Max = UFPSRLChatSettings::Get()->MaxClientChatHistory;
	if (ClientHistory.Num() > Max)
	{
		ClientHistory.RemoveAt(0, ClientHistory.Num() - Max);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] Received #%d %s: %s"), Message.MessageId, *Message.DisplayName, *Message.Text);
	OnMessageAdded.Broadcast(Arrived);
}

void UFPSRLChatSubsystem::ClientMergeHistory(const TArray<FFPSRLChatMessage>& Messages)
{
	TMap<int32, FFPSRLChatMessage> ById;
	for (const FFPSRLChatMessage& Message : ClientHistory)
	{
		ById.Add(Message.MessageId, Message);
	}
	for (const FFPSRLChatMessage& Message : Messages)
	{
		ById.Add(Message.MessageId, Message);
	}
	ById.KeySort(TLess<int32>());
	ClientHistory.Reset();
	ById.GenerateValueArray(ClientHistory);
	const int32 Max = UFPSRLChatSettings::Get()->MaxClientChatHistory;
	if (ClientHistory.Num() > Max)
	{
		ClientHistory.RemoveAt(0, ClientHistory.Num() - Max);
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] History received: %d message(s), now %d"), Messages.Num(), ClientHistory.Num());
	OnHistoryReset.Broadcast();
}

void UFPSRLChatSubsystem::ResetSession(const TCHAR* Why)
{
	if (ServerHistory.IsEmpty() && ClientHistory.IsEmpty() && NextMessageId == 1)
	{
		return;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Chat] Session chat cleared (%s): %d server / %d client message(s) dropped"), Why, ServerHistory.Num(), ClientHistory.Num());
	ServerHistory.Reset();
	ClientHistory.Reset();
	LastAcceptedTime.Reset();
	NextMessageId = 1;
	LastSender.Reset();
	OnHistoryReset.Broadcast();
}

FString UFPSRLChatSubsystem::Describe(const UWorld* World) const
{
	const UFPSRLChatSettings* Settings = UFPSRLChatSettings::Get();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const AFPSRLGameState* FPSRLState = Cast<AFPSRLGameState>(GameState);
	const bool bExpedition = FPSRLState && FPSRLState->DepthLayout && FPSRLState->DepthLayout->HasLayout();
	FString Players;
	if (GameState)
	{
		for (const APlayerState* State : GameState->PlayerArray)
		{
			Players += FString::Printf(TEXT("%s%s"), Players.IsEmpty() ? TEXT("") : TEXT(", "), State ? *State->GetPlayerName() : TEXT("?"));
		}
	}
	return FString::Printf(TEXT("chat enabled %s (%s) | map %s | players [%s] | server history %d (next id %d) | client history %d | last sender '%s' | last message id %d | rate limit: %d tracked, last rejected player %d, cooldown %.2f s"),
		(bExpedition ? Settings->bChatEnabledInExpedition : Settings->bChatEnabledInLobby) ? TEXT("yes") : TEXT("no"), bExpedition ? TEXT("expedition") : TEXT("lobby"),
		World ? *World->GetMapName() : TEXT("?"), *Players, ServerHistory.Num(), NextMessageId, ClientHistory.Num(), *LastSender,
		ClientHistory.IsEmpty() ? 0 : ClientHistory.Last().MessageId, LastAcceptedTime.Num(), LastRejectedPlayerId, Settings->ChatMessageCooldown);
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLChatDebugCommand(TEXT("fpsrl.Chat.Debug"),
	TEXT("Print the session chat state (history counts, players, last sender, rate limit)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (const UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(World))
		{
			UE_LOG(LogFPSRL, Log, TEXT("[Chat] %s"), *Chat->Describe(World));
		}
	}));
#endif
