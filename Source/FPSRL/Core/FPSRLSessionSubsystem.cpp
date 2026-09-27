// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLSessionSubsystem.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"
#include "FPSRL.h"

#define LOCTEXT_NAMESPACE "FPSRLSession"

const FName UFPSRLSessionSubsystem::StatusKey(TEXT("FPSRL_STATUS"));
const FString UFPSRLSessionSubsystem::StatusLobby(TEXT("Lobby"));
const FString UFPSRLSessionSubsystem::StatusInRun(TEXT("InRun"));

namespace FPSRLSession
{
	// Set by the Host flow (BP_GameInstanceBase, Create Advanced Session extra settings); the Menu searched on it too.
	static const FName GameTagKey(TEXT("FPSRLGameTag"));
	static const FString GameTagValue(TEXT("FPSRL_v1"));
}

void UFPSRLSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PreLoginHandle = FGameModeEvents::GameModePreLoginEvent.AddUObject(this, &ThisClass::HandlePreLogin);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
	}
}

void UFPSRLSessionSubsystem::Deinitialize()
{
	FGameModeEvents::GameModePreLoginEvent.Remove(PreLoginHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (GEngine)
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
	Super::Deinitialize();
}

FText UFPSRLSessionSubsystem::GetRunInProgressMessage()
{
	return LOCTEXT("RunInSession", "Unable to join because a run is in session.");
}

// --- Rule and status (host) ------------------------------------------------------------------------------------------

bool UFPSRLSessionSubsystem::IsRunInProgress(const UWorld* World)
{
	const FString LobbyPackage = UFPSRLRunSettings::Get().LobbyMap.ToSoftObjectPath().GetLongPackageName();
	if (!World || LobbyPackage.IsEmpty())
	{
		return false;
	}
	return UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != LobbyPackage;
}

void UFPSRLSessionSubsystem::HandlePreLogin(AGameModeBase* GameMode, const FUniqueNetIdRepl& NewPlayer, FString& ErrorMessage)
{
	if (!ErrorMessage.IsEmpty() || !GameMode || GameMode->GetGameInstance() != GetGameInstance())
	{
		return;	// already refused, or another game instance's server (PIE)
	}
	if (IsRunInProgress(GameMode->GetWorld()))
	{
		ErrorMessage = GetRunInProgressMessage().ToString();
		UE_LOG(LogFPSRL, Log, TEXT("[Session] Refused a join: a run is in session (%s)"), *GetNameSafe(GameMode->GetWorld()));
	}
}

void UFPSRLSessionSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	if (World->GetNetMode() == NM_Client)
	{
		bJoining = false;	// arrived at the host
		return;
	}
	if (World->GetNetMode() == NM_ListenServer)
	{
		AdvertiseStatus(IsRunInProgress(World));
	}
}

void UFPSRLSessionSubsystem::AdvertiseStatus(bool bRunInProgress)
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (!Session)
	{
		return;
	}
	FOnlineSessionSettings Settings = Session->SessionSettings;
	const FString& Status = bRunInProgress ? StatusInRun : StatusLobby;
	Settings.Set(StatusKey, Status, EOnlineDataAdvertisementType::ViaOnlineService);
	Sessions->UpdateSession(NAME_GameSession, Settings, true);
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Advertised status %s"), *Status);
}

// --- Browser (client) ------------------------------------------------------------------------------------------------

void UFPSRLSessionSubsystem::FindSessions()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (bSearching || !Sessions.IsValid())
	{
		if (!Sessions.IsValid())
		{
			OnSessionsFound.Broadcast(false);
		}
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	Search->MaxSearchResults = 100;
	Search->bIsLanQuery = false;
	Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(FPSRLSession::GameTagKey, FPSRLSession::GameTagValue, EOnlineComparisonOp::Equals);

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::HandleFindSessionsComplete));
	bSearching = true;
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		HandleFindSessionsComplete(false);
	}
}

void UFPSRLSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	}
	bSearching = false;

	Rows.Reset();
	if (bWasSuccessful && Search.IsValid())
	{
		for (int32 Index = 0; Index < Search->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = Search->SearchResults[Index];
			FString Tag;
			if (!Result.IsValid() || !Result.Session.SessionSettings.Get(FPSRLSession::GameTagKey, Tag) || Tag != FPSRLSession::GameTagValue)
			{
				continue;	// another game on the shared Steam test app id
			}
			FString Status;
			Result.Session.SessionSettings.Get(StatusKey, Status);

			FFPSRLSessionRow& Row = Rows.AddDefaulted_GetRef();
			Row.HostName = Result.Session.OwningUserName.IsEmpty() ? TEXT("Unknown host") : Result.Session.OwningUserName;
			Row.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Row.Players = FMath::Clamp(Row.MaxPlayers - Result.Session.NumOpenPublicConnections, 0, Row.MaxPlayers);
			Row.bRunInProgress = Status == StatusInRun;
			Row.ResultIndex = Index;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Search %s: %d FPSRL game(s)"), bWasSuccessful ? TEXT("done") : TEXT("failed"), Rows.Num());
	OnSessionsFound.Broadcast(bWasSuccessful);
}

void UFPSRLSessionSubsystem::JoinSession(int32 ResultIndex, APlayerController* PlayerController)
{
	if (bJoining)
	{
		return;
	}
	const FFPSRLSessionRow* Row = Rows.FindByPredicate([ResultIndex](const FFPSRLSessionRow& Candidate) { return Candidate.ResultIndex == ResultIndex; });
	if (!Row || !Search.IsValid() || !Search->SearchResults.IsValidIndex(ResultIndex) || !PlayerController)
	{
		FailJoin(LOCTEXT("GameGone", "That game is no longer available. Refresh the list."));
		return;
	}
	if (Row->bRunInProgress)
	{
		FailJoin(GetRunInProgressMessage());	// the host would refuse it anyway
		return;
	}

	bJoining = true;
	JoinResultIndex = ResultIndex;
	JoiningController = PlayerController;

	// A session left over from an earlier failed join blocks this one; leave it first.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroyBeforeJoinComplete));
		if (Sessions->DestroySession(NAME_GameSession))
		{
			return;
		}
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
	StartJoin();
}

void UFPSRLSessionSubsystem::HandleDestroyBeforeJoinComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
	StartJoin();
}

void UFPSRLSessionSubsystem::StartJoin()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid() || !Search.IsValid() || !Search->SearchResults.IsValidIndex(JoinResultIndex))
	{
		FailJoin(LOCTEXT("GameGone", "That game is no longer available. Refresh the list."));
		return;
	}
	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete));
	if (!Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[JoinResultIndex]))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		FailJoin(LOCTEXT("JoinFailed", "Couldn't join that game."));
	}
}

void UFPSRLSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}

	FString ConnectString;
	APlayerController* PlayerController = JoiningController.Get();
	if (Result == EOnJoinSessionCompleteResult::Success && Sessions.IsValid() && PlayerController
		&& Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString))
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Session] Joined; travelling to %s"), *ConnectString);
		PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
		return;	// bJoining stays set until we arrive (or the host refuses us: HandleConnectionError)
	}

	switch (Result)
	{
	case EOnJoinSessionCompleteResult::SessionIsFull:
		FailJoin(LOCTEXT("Full", "That game is full."));
		break;
	case EOnJoinSessionCompleteResult::SessionDoesNotExist:
		FailJoin(LOCTEXT("GameGone", "That game is no longer available. Refresh the list."));
		break;
	default:
		FailJoin(LOCTEXT("JoinFailed", "Couldn't join that game."));
		break;
	}
	LeaveJoinedSession();
}

void UFPSRLSessionSubsystem::FailJoin(const FText& Reason)
{
	bJoining = false;
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Join failed: %s"), *Reason.ToString());
	OnJoinFailed.Broadcast(Reason);
}

// --- Refused / dropped connections -----------------------------------------------------------------------------------

void UFPSRLSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	HandleConnectionError(ErrorString);
}

void UFPSRLSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	HandleConnectionError(ErrorString);
}

void UFPSRLSessionSubsystem::HandleConnectionError(const FString& ErrorString)
{
	const FText RunMessage = GetRunInProgressMessage();
	const bool bRefusedForRun = ErrorString.Contains(RunMessage.ToString());
	if (!bJoining && !bRefusedForRun)
	{
		return;	// not a join of ours (e.g. the host left a game in progress; the existing flow handles that)
	}
	// Engine and Blueprint both send the player back to the Menu; the browser there shows why.
	PendingJoinError = bRefusedForRun ? RunMessage : LOCTEXT("ConnectFailed", "Couldn't connect to that game.");
	bJoining = false;
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Connection to host failed: %s"), *ErrorString);
	LeaveJoinedSession();
	OnJoinFailed.Broadcast(PendingJoinError);
}

void UFPSRLSessionSubsystem::LeaveJoinedSession()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) && GetWorld() && GetWorld()->GetNetMode() != NM_ListenServer)
	{
		Sessions->DestroySession(NAME_GameSession);
	}
}

FText UFPSRLSessionSubsystem::ConsumePendingJoinError()
{
	FText Error = PendingJoinError;
	PendingJoinError = FText::GetEmpty();
	return Error;
}

#undef LOCTEXT_NAMESPACE
