// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLVoiceSubsystem.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "Components/AudioComponent.h"
#include "Core/FPSRLPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/VoiceInterface.h"
#include "Net/DataBunch.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Social/FPSRLUserSettings.h"
#include "VoicePacketImpl.h"
#include "FPSRL.h"

#define LOCTEXT_NAMESPACE "FPSRLVoice"

namespace FPSRLVoice
{
	constexpr uint8 LocalUser = 0;

	/** A destroy this soon after a travel started is the travel replacing a PlayerState, not the player leaving. */
	constexpr double TravelGraceSeconds = 60.0;

#if !UE_BUILD_SHIPPING
	static TAutoConsoleVariable<float> CVarForceThreshold(TEXT("fpsrl.Voice.ForceThreshold"), -1.f,
		TEXT("Test: voice activity threshold to use instead of the setting (headless tests in a quiet room use 0); -1 = off."));
#endif
}

// --- Subsystem -----------------------------------------------------------------------------------------------------

UFPSRLVoiceSubsystem* UFPSRLVoiceSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UFPSRLVoiceSubsystem>() : nullptr;
}

void UFPSRLVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BeginHandle = AFPSRLPlayerState::OnPlayerStateBegin.AddUObject(this, &ThisClass::HandlePlayerStateBegin);
	ChangedHandle = AFPSRLPlayerState::OnPlayerStateChanged.AddUObject(this, &ThisClass::HandlePlayerStateChanged);
	EndHandle = AFPSRLPlayerState::OnPlayerStateEnd.AddUObject(this, &ThisClass::HandlePlayerStateEnd);
	SettingsHandle = UFPSRLUserSettings::OnChanged.AddUObject(this, &ThisClass::HandleSettingsChanged);
	TravelHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &ThisClass::HandleSeamlessTravelStart);

	if (IsRunningDedicatedServer())
	{
		return;
	}
	// Voice exists only inside an online session: re-check whenever one starts or ends.
	if (IOnlineSubsystem* Online = Online::GetSubsystem(GetGameWorld()); Online && Online->GetSessionInterface())
	{
		IOnlineSessionPtr Sessions = Online->GetSessionInterface();
		SessionCreatedHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateWeakLambda(this, [this](FName, bool) { RefreshTransmit(); }));
		SessionJoinedHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateWeakLambda(this, [this](FName, EOnJoinSessionCompleteResult::Type) { RefreshTransmit(); }));
		SessionEndedHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateWeakLambda(this, [this](FName, bool) { RefreshTransmit(); }));
	}
	if (TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice())
	{
		TalkingHandle = Voice->AddOnPlayerTalkingStateChangedDelegate_Handle(FOnPlayerTalkingStateChangedDelegate::CreateUObject(this, &ThisClass::HandleTalkingStateChanged));
		UE_LOG(LogFPSRL, Log, TEXT("[Voice] voice interface ready (%s)"), IOnlineSubsystem::Get() ? *IOnlineSubsystem::Get()->GetSubsystemName().ToString() : TEXT("?"));
	}
	else
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Voice] no voice interface: voice chat is unavailable (the game runs on without it)"));
	}
	ApplyVolumes();
}

void UFPSRLVoiceSubsystem::Deinitialize()
{
	AFPSRLPlayerState::OnPlayerStateBegin.Remove(BeginHandle);
	AFPSRLPlayerState::OnPlayerStateChanged.Remove(ChangedHandle);
	AFPSRLPlayerState::OnPlayerStateEnd.Remove(EndHandle);
	UFPSRLUserSettings::OnChanged.Remove(SettingsHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(TravelHandle);
	if (IOnlineSubsystem* Online = Online::GetSubsystem(GetGameWorld()); Online && Online->GetSessionInterface())
	{
		Online->GetSessionInterface()->ClearOnCreateSessionCompleteDelegate_Handle(SessionCreatedHandle);
		Online->GetSessionInterface()->ClearOnJoinSessionCompleteDelegate_Handle(SessionJoinedHandle);
		Online->GetSessionInterface()->ClearOnDestroySessionCompleteDelegate_Handle(SessionEndedHandle);
	}
	if (TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice())
	{
		Voice->ClearOnPlayerTalkingStateChangedDelegate_Handle(TalkingHandle);
		if (bTransmitting)
		{
			Voice->StopNetworkedVoice(FPSRLVoice::LocalUser);
		}
	}
	Super::Deinitialize();
}

TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> UFPSRLVoiceSubsystem::GetVoice() const
{
	IOnlineSubsystem* Online = Online::GetSubsystem(GetGameWorld());
	return Online ? Online->GetVoiceInterface() : nullptr;
}

UWorld* UFPSRLVoiceSubsystem::GetGameWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

APlayerController* UFPSRLVoiceSubsystem::GetLocalController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetFirstLocalPlayerController(GetGameWorld()) : nullptr;
}

EFPSRLVoiceStatus UFPSRLVoiceSubsystem::GetStatus() const
{
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (!Voice)
	{
		return EFPSRLVoiceStatus::Unavailable;
	}
	if (!UFPSRLUserSettings::Get()->bVoiceChatEnabled)
	{
		return EFPSRLVoiceStatus::TurnedOff;
	}
	const UWorld* World = GetGameWorld();
	IOnlineSubsystem* Online = Online::GetSubsystem(World);
	const IOnlineSessionPtr Sessions = Online ? Online->GetSessionInterface() : nullptr;
	if (!World || World->GetNetMode() == NM_Standalone || !Sessions || Sessions->GetNumSessions() == 0)
	{
		return EFPSRLVoiceStatus::NotInSession;
	}
	return Voice->IsHeadsetPresent(FPSRLVoice::LocalUser) ? EFPSRLVoiceStatus::Ready : EFPSRLVoiceStatus::NoMicrophone;
}

FText UFPSRLVoiceSubsystem::GetStatusText() const
{
	switch (GetStatus())
	{
	case EFPSRLVoiceStatus::Ready:
		return UFPSRLUserSettings::Get()->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk
			? FText::Format(LOCTEXT("ReadyPTT", "Voice chat on: hold [{0}] to talk"), UFPSRLUserSettings::Get()->PushToTalkKey.GetDisplayName())
			: LOCTEXT("ReadyOpen", "Voice chat on: open mic");
	case EFPSRLVoiceStatus::TurnedOff:
		return LOCTEXT("Off", "Voice chat is off");
	case EFPSRLVoiceStatus::NotInSession:
		return LOCTEXT("NoSession", "Voice chat starts when you are in a multiplayer session");
	case EFPSRLVoiceStatus::NoMicrophone:
		return LOCTEXT("NoMic", "No microphone found (or Windows blocked access): you can still hear teammates");
	default:
		return LOCTEXT("Unavailable", "Voice chat is unavailable");
	}
}

FString UFPSRLVoiceSubsystem::Describe() const
{
	static const TCHAR* StatusNames[] = { TEXT("Ready"), TEXT("TurnedOff"), TEXT("NotInSession"), TEXT("NoMicrophone"), TEXT("Unavailable") };
	auto Join = [](const TSet<int32>& Set)
	{
		TArray<int32> Ids = Set.Array();
		Ids.Sort();
		TArray<FString> Parts;
		for (int32 Id : Ids)
		{
			Parts.Add(FString::FromInt(Id));
		}
		return FString::Join(Parts, TEXT(","));
	};
	return FString::Printf(TEXT("status %s, transmitting %d, talkers %d, speaking [%s], muted [%s]"), StatusNames[static_cast<int32>(GetStatus())],
		bTransmitting, Talkers.Num(), *Join(SpeakingPlayers), *Join(MutedPlayers));
}

// --- Transmit ------------------------------------------------------------------------------------------------------

void UFPSRLVoiceSubsystem::RefreshTransmit()
{
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (!Voice)
	{
		bTransmitting = false;
		return;
	}
	const UFPSRLUserSettings* Settings = UFPSRLUserSettings::Get();
	const EFPSRLVoiceStatus Status = GetStatus();
	const bool bInSession = Status == EFPSRLVoiceStatus::Ready || Status == EFPSRLVoiceStatus::NoMicrophone;
	if (bInSession)
	{
		Voice->RegisterLocalTalker(FPSRLVoice::LocalUser);	// opens the microphone once (and starts sending: corrected below)
		RegisterAllTeammates();
	}
	const bool bWant = bInSession && Voice->IsHeadsetPresent(FPSRLVoice::LocalUser)
		&& (Settings->VoiceInputMode == EFPSRLVoiceInputMode::OpenMic || bPushToTalkHeld);
	if (bWant)
	{
		Voice->StartNetworkedVoice(FPSRLVoice::LocalUser);
	}
	else
	{
		Voice->StopNetworkedVoice(FPSRLVoice::LocalUser);
	}
	if (bWant != bTransmitting)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Voice] %s sending voice (%s)"), bWant ? TEXT("started") : TEXT("stopped"), *Describe());
	}
	ApplyCaptureThreshold();
	bTransmitting = bWant;
	ApplyIncoming();
}

void UFPSRLVoiceSubsystem::SetPushToTalkHeld(bool bHeld)
{
	if (bPushToTalkHeld != bHeld)
	{
		bPushToTalkHeld = bHeld;
		if (UFPSRLUserSettings::Get()->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk)
		{
			RefreshTransmit();
		}
	}
}

// --- Teammates -----------------------------------------------------------------------------------------------------

bool UFPSRLVoiceSubsystem::IsRemotePlayer(const AFPSRLPlayerState* PlayerState) const
{
	const UWorld* World = GetGameWorld();
	if (!PlayerState || PlayerState->GetWorld() != World || PlayerState->IsABot() || PlayerState->IsInactive() || !PlayerState->GetUniqueId().IsValid())
	{
		return false;
	}
	// Not this machine's player (owned by a local controller, or carrying the local player's online id).
	if (const APlayerController* Owner = Cast<APlayerController>(PlayerState->GetOwner()); Owner && Owner->IsLocalController())
	{
		return false;
	}
	const APlayerController* Local = GetLocalController();
	const ULocalPlayer* LocalPlayer = Local ? Local->GetLocalPlayer() : nullptr;
	return !(Local && Local->PlayerState == PlayerState) && !(LocalPlayer && LocalPlayer->GetPreferredUniqueNetId() == PlayerState->GetUniqueId());
}

AFPSRLPlayerState* UFPSRLVoiceSubsystem::FindPlayerState(const FUniqueNetId& Id) const
{
	const UWorld* World = GetGameWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return nullptr;
	}
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState && PlayerState->GetUniqueId().IsValid() && *PlayerState->GetUniqueId() == Id)
		{
			return Cast<AFPSRLPlayerState>(PlayerState);
		}
	}
	return nullptr;
}

void UFPSRLVoiceSubsystem::RegisterTeammate(AFPSRLPlayerState* PlayerState)
{
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (!Voice || !IsRemotePlayer(PlayerState))
	{
		return;
	}
	const int32 PlayerId = PlayerState->GetPlayerId();
	const FUniqueNetIdRepl& Id = PlayerState->GetUniqueId();
	if (!PlayerState->FindComponentByClass<UFPSRLVoiceTalker>())
	{
		// Voice audio hook (volume); one per PlayerState, so a travel's new PlayerState gets its own.
		UFPSRLVoiceTalker* Talker = NewObject<UFPSRLVoiceTalker>(PlayerState);
		Talker->RegisterComponent();
		Talker->RegisterWithPlayerState(PlayerState);
	}
	const bool bNew = !Talkers.Contains(PlayerId);
	if (Voice->RegisterRemoteTalker(*Id))	// needs an online session (the voice interface ignores voice without one)
	{
		Talkers.Add(PlayerId, Id);
		if (bNew)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[Voice] teammate %s (%d) registered"), *PlayerState->GetPlayerName(), PlayerId);
		}
		ApplyIncoming();
	}
}

void UFPSRLVoiceSubsystem::RegisterAllTeammates()
{
	const UWorld* World = GetGameWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (GameState)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			RegisterTeammate(Cast<AFPSRLPlayerState>(PlayerState));
		}
	}
}

void UFPSRLVoiceSubsystem::HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState)
{
	RegisterTeammate(PlayerState);
}

void UFPSRLVoiceSubsystem::HandlePlayerStateChanged(AFPSRLPlayerState* PlayerState)
{
	RegisterTeammate(PlayerState);
}

void UFPSRLVoiceSubsystem::HandlePlayerStateEnd(AFPSRLPlayerState* PlayerState, EEndPlayReason::Type Reason)
{
	if (!PlayerState || PlayerState->GetWorld() != GetGameWorld())
	{
		return;
	}
	const int32 PlayerId = PlayerState->GetPlayerId();
	const FUniqueNetIdRepl* Id = Talkers.Find(PlayerId);
	if (!Id)
	{
		return;
	}
	// A Depth travel replaces PlayerStates (the teammate stays registered: same online id). Otherwise they left.
	if (Reason != EEndPlayReason::Destroyed || FPlatformTime::Seconds() - LastTravelStart < FPSRLVoice::TravelGraceSeconds)
	{
		return;
	}
	if (TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice(); Voice && Id->IsValid())
	{
		Voice->UnregisterRemoteTalker(**Id);
	}
	Talkers.Remove(PlayerId);
	MutedPlayers.Remove(PlayerId);
	SetSpeaking(PlayerId, false);
	UE_LOG(LogFPSRL, Log, TEXT("[Voice] teammate %s (%d) left: voice removed"), *PlayerState->GetPlayerName(), PlayerId);
}

void UFPSRLVoiceSubsystem::HandleSeamlessTravelStart(UWorld* World, const FString& LevelName)
{
	LastTravelStart = FPlatformTime::Seconds();
}

// --- Speaking ------------------------------------------------------------------------------------------------------

void UFPSRLVoiceSubsystem::HandleTalkingStateChanged(TSharedRef<const FUniqueNetId> TalkerId, bool bIsTalking)
{
	// This player's own microphone: the capture's activity detection says it hears speech (the Settings mic check).
	const APlayerController* Local = GetLocalController();
	const ULocalPlayer* LocalPlayer = Local ? Local->GetLocalPlayer() : nullptr;
	const FUniqueNetIdRepl LocalId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	if (LocalId.IsValid() && *LocalId == *TalkerId)
	{
		if (bLocalSpeaking != bIsTalking)
		{
			bLocalSpeaking = bIsTalking;
			if (bIsTalking && !bLoggedLocalSpeech)
			{
				bLoggedLocalSpeech = true;
				UE_LOG(LogFPSRL, Log, TEXT("[Voice] your microphone picked up speech (sending: %d)"), bTransmitting);
			}
			OnLocalSpeakingChanged.Broadcast();
		}
		return;
	}
	// The voice interface reports every talker it knows, this player included: only registered teammates matter here,
	// found by the server-replicated online id they were registered with (the GameState's player list may not have
	// arrived yet, and the interface reports a state only once per change).
	int32 PlayerId = INDEX_NONE;
	for (const TPair<int32, FUniqueNetIdRepl>& Pair : Talkers)
	{
		if (Pair.Value.IsValid() && *Pair.Value == *TalkerId)
		{
			PlayerId = Pair.Key;
			break;
		}
	}
	if (PlayerId == INDEX_NONE)
	{
		const AFPSRLPlayerState* PlayerState = FindPlayerState(*TalkerId);
		PlayerId = PlayerState && IsRemotePlayer(PlayerState) ? PlayerState->GetPlayerId() : INDEX_NONE;
	}
	if (PlayerId != INDEX_NONE)
	{
		SetSpeaking(PlayerId, bIsTalking && !IsMuted(PlayerId) && UFPSRLUserSettings::Get()->bVoiceChatEnabled);
	}
}

void UFPSRLVoiceSubsystem::SetSpeaking(int32 PlayerId, bool bSpeaking)
{
	const bool bWas = SpeakingPlayers.Contains(PlayerId);
	if (bWas == bSpeaking)
	{
		return;
	}
	if (bSpeaking)
	{
		SpeakingPlayers.Add(PlayerId);
	}
	else
	{
		SpeakingPlayers.Remove(PlayerId);
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[Voice] player %d %s speaking"), PlayerId, bSpeaking ? TEXT("started") : TEXT("stopped"));
	if (bSpeaking && !HeardPlayers.Contains(PlayerId))
	{
		HeardPlayers.Add(PlayerId);
		UE_LOG(LogFPSRL, Log, TEXT("[Voice] hearing teammate %d (first time this session)"), PlayerId);
	}
	OnSpeakingChanged.Broadcast(PlayerId, bSpeaking);
}

// --- Mute / incoming -----------------------------------------------------------------------------------------------

void UFPSRLVoiceSubsystem::SetMuted(int32 PlayerId, bool bMuted)
{
	if (MutedPlayers.Contains(PlayerId) == bMuted)
	{
		return;
	}
	if (bMuted)
	{
		MutedPlayers.Add(PlayerId);
		SetSpeaking(PlayerId, false);
	}
	else
	{
		MutedPlayers.Remove(PlayerId);
	}
	ApplyIncoming();
	UE_LOG(LogFPSRL, Log, TEXT("[Voice] player %d %s on this machine"), PlayerId, bMuted ? TEXT("muted") : TEXT("unmuted"));
	OnMuteChanged.Broadcast(PlayerId, bMuted);
}

void UFPSRLVoiceSubsystem::ApplyIncoming()
{
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (!Voice)
	{
		return;
	}
	// Local mutes only (bIsSystemWide false): the voice interface drops the teammate's packets on this machine; the
	// server, the teammate and everyone else are unaffected. Voice Chat off mutes everyone here the same way.
	const bool bVoiceOn = UFPSRLUserSettings::Get()->bVoiceChatEnabled;
	for (const TPair<int32, FUniqueNetIdRepl>& Pair : Talkers)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		const bool bMute = !bVoiceOn || MutedPlayers.Contains(Pair.Key);
		if (bMute != Voice->IsMuted(FPSRLVoice::LocalUser, *Pair.Value))
		{
			bMute ? Voice->MuteRemoteTalker(FPSRLVoice::LocalUser, *Pair.Value, false) : Voice->UnmuteRemoteTalker(FPSRLVoice::LocalUser, *Pair.Value, false);
		}
		if (bMute)
		{
			SetSpeaking(Pair.Key, false);
		}
	}
}

// --- Settings ------------------------------------------------------------------------------------------------------

void UFPSRLVoiceSubsystem::HandleSettingsChanged()
{
	ApplyVolumes();
	ApplyCaptureThreshold();
	ApplyOutputDevice();
	RefreshTransmit();
}

void UFPSRLVoiceSubsystem::ApplyVolumes()
{
	const UFPSRLUserSettings* Settings = UFPSRLUserSettings::Get();
	// Outgoing: the voice capture's own gain (read for every captured buffer).
	if (IConsoleVariable* MicGain = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain")))
	{
		MicGain->Set(Settings->MicrophoneVolume, ECVF_SetByGameSetting);
	}
	// Incoming: each teammate's voice audio playing now (new ones get it in NotifyVoiceAudio), and the gain of the
	// external output device route.
	for (int32 Index = VoiceAudio.Num() - 1; Index >= 0; --Index)
	{
		if (UAudioComponent* Audio = VoiceAudio[Index].Get())
		{
			Audio->SetVolumeMultiplier(Settings->VoiceChatVolume);
		}
		else
		{
			VoiceAudio.RemoveAtSwap(Index);
		}
	}
	if (IConsoleVariable* PatchGain = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.DefaultPatchGain")))
	{
		PatchGain->Set(Settings->VoiceChatVolume, ECVF_SetByGameSetting);
	}
}

void UFPSRLVoiceSubsystem::NotifyVoiceAudio(UAudioComponent* AudioComponent, bool bStarted)
{
	if (!AudioComponent)
	{
		return;
	}
	if (bStarted)
	{
		AudioComponent->SetVolumeMultiplier(UFPSRLUserSettings::Get()->VoiceChatVolume);
		UE_LOG(LogFPSRL, Verbose, TEXT("[Voice] a teammate's voice started playing here at volume %.2f"), AudioComponent->VolumeMultiplier);
		VoiceAudio.AddUnique(AudioComponent);
	}
	else
	{
		VoiceAudio.Remove(AudioComponent);
	}
}

TArray<FString> UFPSRLVoiceSubsystem::GetOutputDevices() const
{
	TArray<FString> Names;
	FAudioDeviceHandle AudioDevice = GEngine ? GEngine->GetMainAudioDevice() : FAudioDeviceHandle();
	const Audio::FMixerDevice* Mixer = AudioDevice ? static_cast<const Audio::FMixerDevice*>(AudioDevice.GetAudioDevice()) : nullptr;
	Audio::IAudioMixerPlatformInterface* Platform = Mixer ? Mixer->GetAudioMixerPlatform() : nullptr;	// every UE5 audio device is a mixer device
	uint32 Count = 0;
	if (Platform && Platform->GetNumOutputDevices(Count))
	{
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			Audio::FAudioPlatformDeviceInfo Info;
			if (Platform->GetOutputDeviceInfo(Index, Info) && !Info.Name.IsEmpty())
			{
				Names.AddUnique(Info.Name);
			}
		}
	}
	return Names;
}

void UFPSRLVoiceSubsystem::ApplyOutputDevice()
{
	const FString& Device = UFPSRLUserSettings::Get()->VoiceOutputDevice;
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (!Voice || Device == AppliedOutputDevice)
	{
		return;
	}
	Voice->DisconnectAllEndpoints();	// back to the game's own output
	if (!Device.IsEmpty() && !GetOutputDevices().Contains(Device))
	{
		UE_LOG(LogFPSRL, Warning, TEXT("[Voice] output device '%s' not found: teammates play through the game's output"), *Device);
	}
	else if (!Device.IsEmpty())
	{
		Voice->PatchRemoteTalkerOutputToEndpoint(Device, true);	// voices only go to that device
	}
	AppliedOutputDevice = Device;
	UE_LOG(LogFPSRL, Log, TEXT("[Voice] teammates' voices play on %s"), Device.IsEmpty() ? TEXT("the game's output") : *Device);
}

// --- Talker --------------------------------------------------------------------------------------------------------

void UFPSRLVoiceTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	Super::OnTalkingBegin(AudioComponent);
	Playing = AudioComponent;
	if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(this))
	{
		Voice->NotifyVoiceAudio(AudioComponent, true);
	}
}

void UFPSRLVoiceTalker::OnTalkingEnd()
{
	Super::OnTalkingEnd();
	if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(this))
	{
		Voice->NotifyVoiceAudio(Playing.Get(), false);
	}
	Playing.Reset();
}

// --- Voice channel -------------------------------------------------------------------------------------------------

void UFPSRLVoiceChannel::ReceivedBunch(FInBunch& Bunch)
{
	// Server: read the packets from a copy first; every one must come from this connection's own player.
	if (Connection && Connection->Driver && Connection->Driver->ServerConnection == nullptr)
	{
		static bool bAnnounced = false;
		if (!bAnnounced)
		{
			bAnnounced = true;
			UE_LOG(LogFPSRL, Log, TEXT("[Voice] voice channel with sender check active on the server"));
		}
		const FUniqueNetIdRepl& Owner = Connection->PlayerId;
		FInBunch Peek(Bunch, true);
		while (!Peek.AtEnd() && !Peek.IsError())
		{
			FVoicePacketImpl Packet;
			Packet.Serialize(Peek);
			const FUniqueNetIdPtr Sender = Packet.GetSender();
			if (Peek.IsError() || !Owner.IsValid() || !Sender.IsValid() || !(*Sender == *Owner))
			{
				static double LastWarning = 0.0;
				if (FPlatformTime::Seconds() - LastWarning > 5.0)
				{
					LastWarning = FPlatformTime::Seconds();
					UE_LOG(LogFPSRL, Warning, TEXT("[Voice] dropped voice from %s claiming to be %s"), Owner.IsValid() ? *Owner.ToString() : TEXT("(no id)"),
						Sender.IsValid() ? *Sender->ToString() : TEXT("(none)"));
				}
				return;
			}
		}
	}
	Super::ReceivedBunch(Bunch);
}

#undef LOCTEXT_NAMESPACE

void UFPSRLVoiceSubsystem::ResetSession()
{
	TSharedPtr<IOnlineVoice, ESPMode::ThreadSafe> Voice = GetVoice();
	if (Voice)
	{
		Voice->StopNetworkedVoice(FPSRLVoice::LocalUser);
		for (const TPair<int32, FUniqueNetIdRepl>& Pair : Talkers)
		{
			if (Pair.Value.IsValid())
			{
				Voice->UnmuteRemoteTalker(FPSRLVoice::LocalUser, *Pair.Value, false);
				Voice->UnregisterRemoteTalker(*Pair.Value);
			}
		}
	}
	for (int32 PlayerId : SpeakingPlayers.Array())
	{
		SetSpeaking(PlayerId, false);
	}
	Talkers.Reset();
	MutedPlayers.Reset();
	HeardPlayers.Reset();
	bLocalSpeaking = false;
	bLoggedLocalSpeech = false;
	VoiceAudio.Reset();
	bPushToTalkHeld = false;
	if (bTransmitting)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Voice] session over: stopped sending voice"));
	}
	bTransmitting = false;
}

float UFPSRLVoiceSubsystem::GetOpenMicThreshold()
{
	// The engine's default (0.08 of full scale) missed normal speech on both playtest microphones (v0.1.37): the
	// sensitivity curve puts the default (0.7) at about 0.009 and lets the player go either way.
	const float Sensitivity = FMath::Clamp(UFPSRLUserSettings::Get()->OpenMicSensitivity, 0.f, 1.f);
	return FMath::Max(0.001f, 0.1f * FMath::Square(1.f - Sensitivity));
}

void UFPSRLVoiceSubsystem::ApplyCaptureThreshold()
{
	// Push-to-Talk: everything said while the key is held goes out (no activity filter); open mic: the threshold.
	const bool bPushToTalk = UFPSRLUserSettings::Get()->VoiceInputMode == EFPSRLVoiceInputMode::PushToTalk;
	float Threshold = bPushToTalk ? 0.f : GetOpenMicThreshold();
#if !UE_BUILD_SHIPPING
	if (FPSRLVoice::CVarForceThreshold.GetValueOnGameThread() >= 0.f)
	{
		Threshold = FPSRLVoice::CVarForceThreshold.GetValueOnGameThread();
	}
#endif
	for (const TCHAR* Name : { TEXT("voice.SilenceDetectionThreshold"), TEXT("voice.MicNoiseGateThreshold") })
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name); Variable && Variable->GetFloat() != Threshold)
		{
			Variable->Set(Threshold, ECVF_SetByGameSetting);
		}
	}
}
