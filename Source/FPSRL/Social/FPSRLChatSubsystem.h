// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Social/FPSRLChatTypes.h"
#include "FPSRLChatSubsystem.generated.h"

class AController;
class AGameModeBase;
class APlayerController;
class UFPSRLChatFilter;

DECLARE_MULTICAST_DELEGATE_OneParam(FFPSRLChatMessageEvent, const FFPSRLChatMessage& /*Message*/);
DECLARE_MULTICAST_DELEGATE(FFPSRLChatHistoryEvent);

/**
 * Session text chat. Lives on the GameInstance, so it outlives every level: room streaming, traversals, the Lobby ->
 * expedition travel and each Depth's server travel never touch it. Reset when a session is created or joined and when
 * the player is back in the main menu: chat history exists only for the session, in memory.
 *
 * Server side (the listen host): the authoritative rolling history, the message ids and the per-player rate limit.
 * A client asks through AFPSRLPlayerController::ServerSendChatMessage (text only); the server validates, stamps the
 * sender / id / time itself, keeps it and sends that ONE message to every player's controller (reliable client RPC).
 * A player who joins gets the recent history once (PostLogin).
 *
 * Client side (every player, the host included): its own rolling history, which the chat widget shows.
 */
UCLASS()
class FPSRL_API UFPSRLChatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UFPSRLChatSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- Server ----

	/** Server: validate, filter, stamp and broadcast a message from Sender. Returns the reason when refused. */
	EFPSRLChatRejectReason ServerSubmit(APlayerController* Sender, const FString& RawText);

	/** Server: send the recent history to one player (on join). */
	void ServerSendHistoryTo(APlayerController* Player) const;

	const TArray<FFPSRLChatMessage>& GetServerHistory() const { return ServerHistory; }

	// ---- Client ----

	/** Client: one message arrived from the server (duplicates by id are ignored). */
	void ClientAddMessage(const FFPSRLChatMessage& Message);

	/** Client: the server's recent history (on join); merged by id, kept in order. */
	void ClientMergeHistory(const TArray<FFPSRLChatMessage>& Messages);

	const TArray<FFPSRLChatMessage>& GetClientHistory() const { return ClientHistory; }

	/** Fired on this machine for each new message in the client history. */
	FFPSRLChatMessageEvent OnMessageAdded;

	/** Fired when the client history is replaced (history sync, session reset). */
	FFPSRLChatHistoryEvent OnHistoryReset;

	// ---- Session ----

	/** Forget everything (a new session begins, or we are back in the menu). */
	void ResetSession(const TCHAR* Why);

	/** Debug: the chat state in one line. */
	FString Describe(const UWorld* World) const;

private:
	void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);
	EFPSRLChatRejectReason CheckAllowed(const APlayerController* Sender) const;
	const UFPSRLChatFilter* GetFilter() const;

	TArray<FFPSRLChatMessage> ServerHistory;
	TArray<FFPSRLChatMessage> ClientHistory;
	int32 NextMessageId = 1;

	/** Server: the last accepted message time (platform seconds) per sender PlayerId. */
	TMap<int32, double> LastAcceptedTime;
	FString LastSender;
	int32 LastRejectedPlayerId = INDEX_NONE;

	UPROPERTY(Transient)
	mutable TObjectPtr<UFPSRLChatFilter> Filter;

	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
};
