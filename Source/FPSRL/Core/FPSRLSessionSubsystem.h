// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "FPSRLSessionSubsystem.generated.h"

class AGameModeBase;
class APlayerController;
class FOnlineSessionSearch;
struct FUniqueNetIdRepl;

/** One hosted FPSRL game as the server browser shows it. */
USTRUCT(BlueprintType)
struct FFPSRLSessionRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 Players = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 MaxPlayers = 0;

	/** The host is in a run (not the Lobby): nobody can join until they're back in the Lobby. */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	bool bRunInProgress = false;

	/** Index into the last search's results (what JoinSession takes). */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 ResultIndex = INDEX_NONE;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FFPSRLSessionsFoundEvent, bool /*bSuccess*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FFPSRLJoinFailedEvent, const FText& /*Reason*/);

/**
 * Who can join a hosted game, and finding/joining one from the server browser.
 *
 *  - Rule (server, can't be bypassed): while the host is in a run (any map but the Lobby), new connections are refused
 *    with "Unable to join because a run is in session." Players already in travel with the host (seamless travel)
 *    never pass through login, so they're unaffected. Hooked on the engine's pre-login event, so every game mode
 *    obeys it without a C++ parent.
 *  - Status: the host advertises FPSRL_STATUS = Lobby / InRun on its Steam session, updated on every map load, so the
 *    browser can show it before anyone tries.
 *  - Browser: FindSessions lists FPSRL sessions (FPSRLGameTag = FPSRL_v1, the tag the Host flow sets); JoinSession
 *    joins one of them and travels. A refused or failed join returns the player to the Menu, where the reason waits
 *    in ConsumePendingJoinError.
 */
UCLASS()
class FPSRL_API UFPSRLSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** "Unable to join because a run is in session." */
	static FText GetRunInProgressMessage();

	/** Start a search for hosted FPSRL games (ignored while one is running). OnSessionsFound fires when done. */
	void FindSessions();
	bool IsSearching() const { return bSearching; }
	const TArray<FFPSRLSessionRow>& GetRows() const { return Rows; }

	/** Join a row of the last search and travel to it. On failure, OnJoinFailed fires with the reason. */
	void JoinSession(int32 ResultIndex, APlayerController* PlayerController);
	bool IsJoining() const { return bJoining; }

	/** A join that failed after leaving the Menu (e.g. the host refused it); empty if none. Clears it. */
	FText ConsumePendingJoinError();

	FFPSRLSessionsFoundEvent OnSessionsFound;
	FFPSRLJoinFailedEvent OnJoinFailed;

	/** Session setting key for the host's status, and its two values. */
	static const FName StatusKey;
	static const FString StatusLobby;
	static const FString StatusInRun;

private:
	/** The server's current map is anything but the Lobby. */
	static bool IsRunInProgress(const UWorld* World);

	void HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage);
	void HandlePostLoadMap(UWorld* World);
	void AdvertiseStatus(bool bRunInProgress);

	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleDestroyBeforeJoinComplete(FName SessionName, bool bWasSuccessful);
	void StartJoin();
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void FailJoin(const FText& Reason);

	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleConnectionError(const FString& ErrorString);

	/** Leave our copy of a session we tried to join, so the next Join doesn't fail with "already in a session". */
	void LeaveJoinedSession();

	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FFPSRLSessionRow> Rows;
	bool bSearching = false;

	bool bJoining = false;
	int32 JoinResultIndex = INDEX_NONE;
	TWeakObjectPtr<APlayerController> JoiningController;

	FText PendingJoinError;

	FDelegateHandle PreLoginHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle FindCompleteHandle;
	FDelegateHandle JoinCompleteHandle;
	FDelegateHandle DestroyCompleteHandle;
};
