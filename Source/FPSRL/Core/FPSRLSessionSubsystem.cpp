// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/FPSRLSessionSubsystem.h"
#include "Data/FPSRLRunSettings.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"
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
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyBeforeHostHandle);
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
	bHosting = false;	// any map load ends a Host attempt (the Lobby it opened, or back to the Menu)
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

// --- Hosting ---------------------------------------------------------------------------------------------------------

void UFPSRLSessionSubsystem::HostSession()
{
	if (bHosting)
	{
		return;	// a create is already in flight: extra clicks must not start a second one
	}
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid())
	{
		FailHost(LOCTEXT("NoOnline", "Steam isn't available. Make sure Steam is running."));
		return;
	}
	bHosting = true;
	HostAttempt = 0;
	OnHostStatus.Broadcast(LOCTEXT("Creating", "Creating game..."), false);

	// A session left over from an earlier game (or a create Steam never finished) would make the create fail with
	// "session already exists": clear it first.
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Session] Host: clearing a leftover session first"));
		DestroyBeforeHostHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroyBeforeHostComplete));
		if (Sessions->DestroySession(NAME_GameSession))
		{
			return;
		}
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyBeforeHostHandle);
	}
	StartCreate();
}

void UFPSRLSessionSubsystem::HandleDestroyBeforeHostComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyBeforeHostHandle);
	}
	if (bHosting)
	{
		StartCreate();
	}
}

void UFPSRLSessionSubsystem::StartCreate()
{
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	const FUniqueNetIdRepl UserId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	if (!Sessions.IsValid() || !UserId.IsValid())
	{
		FailHost(LOCTEXT("NotSignedIn", "Not signed in to Steam yet. Wait a moment and try again."));
		return;
	}
	++HostAttempt;

	// Same settings as the old Blueprint flow (Create Advanced Session), so the browser and invites see no difference.
	const IOnlineSubsystem* Online = Online::GetSubsystem(GetWorld());
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = 4;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = Online && Online->GetSubsystemName() == NULL_SUBSYSTEM;
	Settings.bIsDedicated = false;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.Set(FPSRLSession::GameTagKey, FPSRLSession::GameTagValue, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(StatusKey, StatusLobby, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleCreateSessionComplete));
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Host: creating session (attempt %d)"), HostAttempt);
	if (!Sessions->CreateSession(*UserId, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
		FailHost(LOCTEXT("CreateFailed", "Couldn't create the game on Steam. Try again in a moment."));
		return;
	}
	GetGameInstance()->GetTimerManager().SetTimer(HostTimeoutTimer, this, &ThisClass::HandleHostTimeout, HostTimeoutSeconds, false);
}

void UFPSRLSessionSubsystem::HandleHostTimeout()
{
	if (!bHosting)
	{
		return;
	}
	// Steam never answered (seen when hosting right after launch while Steam's network was still starting).
	// Abandon that create and try once more; a second silence gives up with a message.
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
	UE_LOG(LogFPSRL, Warning, TEXT("[Session] Host: Steam didn't answer within %.0fs (attempt %d)"), HostTimeoutSeconds, HostAttempt);
	if (HostAttempt >= 2 || !Sessions.IsValid())
	{
		if (Sessions.IsValid())
		{
			Sessions->DestroySession(NAME_GameSession);
		}
		FailHost(LOCTEXT("SteamSlow", "Steam didn't respond. Check your connection and try again."));
		return;
	}
	OnHostStatus.Broadcast(LOCTEXT("Retrying", "Steam is slow to respond, trying again..."), false);
	DestroyBeforeHostHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroyBeforeHostComplete));
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyBeforeHostHandle);
		StartCreate();
	}
}

void UFPSRLSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld()); Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
	GetGameInstance()->GetTimerManager().ClearTimer(HostTimeoutTimer);
	if (!bHosting)
	{
		return;
	}
	if (!bWasSuccessful)
	{
		FailHost(LOCTEXT("CreateFailed", "Couldn't create the game on Steam. Try again in a moment."));
		return;
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Session] Host: session created, opening the Lobby"));
	OnHostStatus.Broadcast(LOCTEXT("Opening", "Opening the Lobby..."), false);
	// bHosting stays set until the Lobby has loaded (HandlePostLoadMap), so a click during the load can't start over.
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), UFPSRLRunSettings::Get().LobbyMap, true, TEXT("listen"));
}

void UFPSRLSessionSubsystem::FailHost(const FText& Reason)
{
	UE_LOG(LogFPSRL, Warning, TEXT("[Session] Host failed: %s"), *Reason.ToString());
	GetGameInstance()->GetTimerManager().ClearTimer(HostTimeoutTimer);
	bHosting = false;
	OnHostStatus.Broadcast(Reason, true);
}

#undef LOCTEXT_NAMESPACE
