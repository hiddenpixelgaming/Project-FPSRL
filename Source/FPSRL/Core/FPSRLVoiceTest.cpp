// Fill out your copyright notice in the Description page of Project Settings.

// Test only (not in Shipping), for headless multiplayer voice checks:
//  - fpsrl.Voice.TestSession: create a local online session when there is none. The voice interface only processes
//    voice inside a session; real games have the Steam session, a -nosteam IP test has none.
//  - fpsrl.Voice.Mute <PlayerId> <0|1>: mute / unmute a teammate on this machine.
//  - fpsrl.Voice.Log <seconds>: log this machine's voice state and teammate HUD every N seconds ([VoiceTest]); 0 stops.
//  - fpsrl.Voice.Set <setting> <value>: change a voice setting (enabled 0|1, mode open|ptt, ptt 0|1 = hold the key).

#include "Containers/Ticker.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Social/FPSRLUserSettings.h"
#include "Social/FPSRLVoiceSubsystem.h"
#include "UI/FPSRLTeamHUDWidget.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLVoiceTestSessionCommand(TEXT("fpsrl.Voice.TestSession"),
	TEXT("Test: create a local online session if there is none (voice needs one; -nosteam IP tests have none)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		IOnlineSubsystem* Online = Online::GetSubsystem(World);
		IOnlineSessionPtr Sessions = Online ? Online->GetSessionInterface() : nullptr;
		if (!Sessions || Sessions->GetNumSessions() > 0)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[VoiceTest] session: %s"), Sessions ? TEXT("already in one") : TEXT("no session interface"));
			return;
		}
		FOnlineSessionSettings Settings;
		Settings.NumPublicConnections = 4;
		Settings.bIsLANMatch = true;
		Settings.bShouldAdvertise = false;
		const bool bStarted = Sessions->CreateSession(0, NAME_GameSession, Settings);
		UE_LOG(LogFPSRL, Log, TEXT("[VoiceTest] creating a local test session: %d"), bStarted);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRLVoiceMuteCommand(TEXT("fpsrl.Voice.Mute"),
	TEXT("Test: fpsrl.Voice.Mute <PlayerId> <0|1> - mute / unmute a teammate on this machine."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World); Voice && Args.Num() >= 2)
		{
			Voice->SetMuted(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]) != 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRLVoiceSetCommand(TEXT("fpsrl.Voice.Set"),
	TEXT("Test: fpsrl.Voice.Set <enabled 0|1 | mode open|ptt | ptt 0|1 | volume 0-1 | mic 0-2>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			return;
		}
		const FString What = Args[0].ToLower();
		if (What == TEXT("enabled"))
		{
			UFPSRLUserSettings::SetVoiceChatEnabled(FCString::Atoi(*Args[1]) != 0);
		}
		else if (What == TEXT("mode"))
		{
			UFPSRLUserSettings::SetVoiceInputMode(Args[1].ToLower() == TEXT("ptt") ? EFPSRLVoiceInputMode::PushToTalk : EFPSRLVoiceInputMode::OpenMic);
		}
		else if (What == TEXT("ptt"))
		{
			if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World))
			{
				Voice->SetPushToTalkHeld(FCString::Atoi(*Args[1]) != 0);
			}
		}
		else if (What == TEXT("volume"))
		{
			UFPSRLUserSettings::SetVoiceChatVolume(FCString::Atof(*Args[1]));
		}
		else if (What == TEXT("mic"))
		{
			UFPSRLUserSettings::SetMicrophoneVolume(FCString::Atof(*Args[1]));
		}
	}));

namespace FPSRLVoiceTest
{
	static FTSTicker::FDelegateHandle LogTicker;
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLVoiceLogCommand(TEXT("fpsrl.Voice.Log"),
	TEXT("Test: fpsrl.Voice.Log <seconds> - log the voice state and teammate HUD every N seconds ([VoiceTest]); 0 stops."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FTSTicker::RemoveTicker(FPSRLVoiceTest::LogTicker);
		const float Seconds = Args.IsEmpty() ? 2.f : FCString::Atof(*Args[0]);
		if (Seconds <= 0.f || !World)
		{
			return;
		}
		TWeakObjectPtr<UGameInstance> GameInstance = World->GetGameInstance();
		FPSRLVoiceTest::LogTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* Current = GI ? GI->GetWorld() : nullptr;
			const UFPSRLVoiceSubsystem* Voice = GI ? GI->GetSubsystem<UFPSRLVoiceSubsystem>() : nullptr;
			const AFPSRLPlayerController* PC = Current ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(Current)) : nullptr;
			if (Voice)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[VoiceTest] %s (id %d): %s | HUD: %s"), PC && PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("?"),
					PC && PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1, *Voice->Describe(), PC && PC->GetTeamHUD() ? *PC->GetTeamHUD()->DescribeForTest() : TEXT("-"));
			}
			return true;
		}), Seconds);
	}));
#endif

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFPSRLVoiceAtCommand(TEXT("fpsrl.Voice.At"),
	TEXT("Test: fpsrl.Voice.At <seconds> <console command...> - run a console command later (timed multiplayer steps)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2 || !World)
		{
			return;
		}
		const float Seconds = FCString::Atof(*Args[0]);
		const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
		TWeakObjectPtr<UGameInstance> GameInstance = World->GetGameInstance();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Command](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* Current = GI ? GI->GetWorld() : nullptr;
			if (APlayerController* PC = Current ? GI->GetFirstLocalPlayerController(Current) : nullptr)
			{
				UE_LOG(LogFPSRL, Log, TEXT("[VoiceTest] running '%s'"), *Command);
				PC->ConsoleCommand(Command);
			}
			return false;
		}), Seconds);
	}));
#endif

#if !UE_BUILD_SHIPPING
#include "Components/CheckBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/InputKeySelector.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "UI/FPSRLPauseMenuWidget.h"
#include "UObject/UObjectIterator.h"

namespace FPSRLVoiceSettingsTest
{
	struct FState
	{
		int32 Step = 0;
		int32 Problems = 0;
		double StepTime = 0.0;
	};
}

static FAutoConsoleCommandWithWorld GFPSRLVoiceSettingsTestCommand(TEXT("FPSRL.VoiceSettingsTest"),
	TEXT("Test (a player with a teammate): pause > Settings > Voice, as a player uses it ([VoiceSettingsTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLVoiceSettingsTest::FState> S = MakeShared<FPSRLVoiceSettingsTest::FState>();
		S->StepTime = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
			UFPSRLVoiceSubsystem* Voice = GI ? GI->GetSubsystem<UFPSRLVoiceSubsystem>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			auto Check = [S](bool bOk, const FString& What)
			{
				S->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[VoiceSettingsTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			auto KeysFor = [PC]()
			{
				TArray<FString> Keys;
				UEnhancedInputLocalPlayerSubsystem* Input = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
				if (Input && PC->GetPushToTalkAction())
				{
					for (const FKey& Key : Input->QueryKeysMappedToAction(PC->GetPushToTalkAction()))
					{
						Keys.Add(Key.ToString());
					}
				}
				return FString::Join(Keys, TEXT(","));
			};
			if (!PC || !Voice || Now - S->StepTime < 2.0)
			{
				return true;
			}
			S->StepTime = Now;
			UFPSRLPauseMenuWidget* Menu = PC->GetPauseMenu();
			switch (S->Step++)
			{
			case 0:
			{
				// Other mappings on V (the default Push-to-Talk key) would fire together with it.
				for (TObjectIterator<UInputMappingContext> It; It; ++It)
				{
					for (const FEnhancedActionKeyMapping& Mapping : It->GetMappings())
					{
						if (Mapping.Key == EKeys::V && *It != nullptr && It->GetOuter() != PC)
						{
							UE_LOG(LogFPSRL, Log, TEXT("[VoiceSettingsTest] note: %s also maps V to %s"), *It->GetPathName(), Mapping.Action ? *Mapping.Action->GetName() : TEXT("?"));
						}
					}
				}
				Check(KeysFor() == TEXT("V"), FString::Printf(TEXT("Push-to-Talk action mapped to '%s' (expect V)"), *KeysFor()));
				PC->OpenPauseMenu();
				return true;
			}
			case 1:
				Menu->ShowVoiceTab();
				Check(Menu->IsSettingsPageShown() && Menu->DescribeVoiceSettings().StartsWith(TEXT("tab 1, voice on 1, volume 100%, mic 100%, mode 'Open Mic', key V (enabled 0)")),
					FString::Printf(TEXT("pause > Settings > Voice: %s"), *Menu->DescribeVoiceSettings()));
				return true;
			case 2:
			{
				// Mute the first teammate with their box, lower the voice volume, switch to Push to Talk.
				TArray<UWidget*> All;
				Menu->WidgetTree->GetAllWidgets(All);
				UCheckBox* FirstMute = nullptr;
				for (UWidget* Widget : All)
				{
					if (UCheckBox* Box = Cast<UCheckBox>(Widget); Box && Box->GetParent() && Box->GetParent()->GetParent() && Box->GetParent()->GetParent()->GetFName() == TEXT("MuteList"))
					{
						FirstMute = Box;
						break;
					}
				}
				if (FirstMute)
				{
					FirstMute->SetIsChecked(true);
					FirstMute->OnCheckStateChanged.Broadcast(true);
				}
				if (USlider* Slider = Cast<USlider>(Menu->GetWidgetFromName(TEXT("VoiceVolumeSlider"))))
				{
					Slider->SetValue(0.5f);
					Slider->OnValueChanged.Broadcast(0.5f);
				}
				if (UButton* Mode = Cast<UButton>(Menu->GetWidgetFromName(TEXT("InputModeButton"))))
				{
					Mode->OnClicked.Broadcast();
				}
				if (UButton* Detect = Cast<UButton>(Menu->GetWidgetFromName(TEXT("AutoDetectButton"))))
				{
					Detect->OnClicked.Broadcast();	// listens for 4 s
				}
				return true;
			}
			case 3:
				Check(Menu->DescribeVoiceSettings().Contains(TEXT("volume 50%")) && Menu->DescribeVoiceSettings().Contains(TEXT("mode 'Push to Talk', key V (enabled 1)"))
					&& Menu->DescribeVoiceSettings().Contains(TEXT(":1]")) && Voice->Describe().Contains(TEXT("transmitting 0")) && !Voice->Describe().Contains(TEXT("muted []")),
					FString::Printf(TEXT("muted, 50%%, Push to Talk: %s | %s"), *Menu->DescribeVoiceSettings(), *Voice->Describe()));
				PC->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: the Voice tab
				return true;
			case 4:
				// Rebind Push-to-Talk to B, as the key selector does.
				Menu->ShowVoiceTab();
				if (UInputKeySelector* Selector = Cast<UInputKeySelector>(Menu->GetWidgetFromName(TEXT("PushToTalkKeySelector"))))
				{
					Selector->OnKeySelected.Broadcast(FInputChord(EKeys::B));
				}
				return true;
			case 5:
				Check(!Voice->IsDetectingMicrophone() && !Voice->GetDetectResult().IsEmpty() && !Voice->GetOpenMicrophoneName().IsEmpty(),
					FString::Printf(TEXT("auto-detect finished: '%s'; microphone in use '%s'; devices [%s]"), *Voice->GetDetectResult().ToString(), *Voice->GetOpenMicrophoneName(),
						*FString::Join(UFPSRLVoiceSubsystem::GetMicrophoneDevices(), TEXT(" | "))));
				Check(KeysFor() == TEXT("B") && Menu->DescribeVoiceSettings().Contains(TEXT("key B")), FString::Printf(TEXT("rebound: action keys '%s', %s"), *KeysFor(), *Menu->DescribeVoiceSettings()));
				Voice->SetPushToTalkHeld(true);
				Check(Voice->IsTransmitting(), FString::Printf(TEXT("Push-to-Talk held: %s"), *Voice->Describe()));
				Voice->SetPushToTalkHeld(false);
				Check(!Voice->IsTransmitting(), FString::Printf(TEXT("released: %s"), *Voice->Describe()));
				// Put everything back (shared settings file).
				UFPSRLUserSettings::SetPushToTalkKey(EKeys::V);
				UFPSRLUserSettings::SetVoiceInputMode(EFPSRLVoiceInputMode::OpenMic);
				UFPSRLUserSettings::SetVoiceChatVolume(1.f);
				UFPSRLUserSettings::SetMicrophoneVolume(1.f);
				UFPSRLUserSettings::SetMicrophoneDevice(FString());
				UFPSRLUserSettings::SetVoiceChatEnabled(true);
				Menu->ShowVoiceTab();
				return true;
			case 6:	// key remaps apply on the next frame
				Check(KeysFor() == TEXT("V") && Voice->IsTransmitting(), FString::Printf(TEXT("restored: keys '%s', %s"), *KeysFor(), *Voice->Describe()));
				PC->ClosePauseMenu();
				UE_LOG(LogFPSRL, Log, TEXT("[VoiceSettingsTest] done: %d problem(s)"), S->Problems);
				return false;
			default:
				return false;
			}
		}));
	}));
#endif

#if !UE_BUILD_SHIPPING
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/VoiceChannel.h"
#include "GameFramework/GameStateBase.h"
#include "Net/VoiceDataCommon.h"

namespace FPSRLVoiceForgeTest
{
	/** A voice packet in the engine's wire format that claims somebody else as its sender. */
	class FForgedPacket : public FVoicePacket
	{
	public:
		explicit FForgedPacket(const FString& InSender) : Sender(InSender) {}
		virtual uint16 GetTotalPacketSize() override { return 64; }
		virtual uint16 GetBufferSize() override { return 32; }
		virtual FUniqueNetIdPtr GetSender() override { return nullptr; }
		virtual bool IsReliable() override { return true; }
		virtual void Serialize(FArchive& Ar) override
		{
			FString SenderStr = Sender;
			int16 Amplitude = 20000;
			uint16 Length = 32;
			uint64 SampleCount = 0;
			Ar << SenderStr << Amplitude << Length << SampleCount;
			uint8 Bytes[32] = {};
			Ar.Serialize(Bytes, Length);
		}
	private:
		FString Sender;
	};
}

static FAutoConsoleCommandWithWorld GFPSRLVoiceForgeTestCommand(TEXT("fpsrl.Voice.ForgeTest"),
	TEXT("Test (a client): send voice packets that claim another player as the sender; the server must drop them."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const APlayerController* PC = World && World->GetGameInstance() ? World->GetGameInstance()->GetFirstLocalPlayerController(World) : nullptr;
		UNetConnection* Connection = World && World->GetNetDriver() ? World->GetNetDriver()->ServerConnection.Get() : nullptr;
		UVoiceChannel* Channel = Connection ? Connection->GetVoiceChannel() : nullptr;
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		FString Victim;
		for (const APlayerState* Other : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
		{
			// Somebody who is neither this client nor the host (PlayerId order: the host joined first).
			if (Other && PC && Other != PC->PlayerState && Other->GetUniqueId().IsValid() && Other != GameState->PlayerArray[0])
			{
				Victim = Other->GetUniqueId().ToString();
			}
		}
		if (!Channel || Victim.IsEmpty())
		{
			UE_LOG(LogFPSRL, Warning, TEXT("[VoiceTest] forge test: no voice channel or no third player"));
			return;
		}
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Channel->AddVoicePacket(MakeShared<FPSRLVoiceForgeTest::FForgedPacket>(Victim));
		}
		UE_LOG(LogFPSRL, Log, TEXT("[VoiceTest] forge test: sent 20 voice packets claiming to be %s"), *Victim);
	}));
#endif

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GFPSRLVoiceSelfTestCommand(TEXT("fpsrl.Voice.SelfTest"),
	TEXT("Test: run the Settings voice test (record 3 s, play back through the receive path)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (UFPSRLVoiceSubsystem* Voice = UFPSRLVoiceSubsystem::Get(World))
		{
			Voice->StartVoiceTest();
		}
	}));
#endif
