// Fill out your copyright notice in the Description page of Project Settings.

// Test only:
//  - FPSRL.ChatTest (solo, any level with a pawn): the open-chat action opens the input with focus and the game keeps
//    running; Enter sends; the server rate-limits, refuses empty / too long text and strips control characters;
//    Esc closes without sending and keeps the draft; compact lines fade but stay in the history; both histories cap.
//  - fpsrl.Chat.Say <text>: send a chat message as the local player (the normal client path).
//  - fpsrl.Chat.AutoSay <seconds>: keep sending numbered messages (multiplayer and level-travel checks); 0 stops.

#include "Containers/Ticker.h"
#include "Misc/Base64.h"
#include "Core/FPSRLPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "Social/FPSRLChatSettings.h"
#include "Social/FPSRLChatSubsystem.h"
#include "Social/FPSRLProfanityFilter.h"
#include "Social/FPSRLUserSettings.h"
#include "UI/FPSRLPauseMenuWidget.h"
#include "Components/CheckBox.h"
#include "UI/FPSRLChatWidget.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLChatTest
{
	struct FState
	{
		int32 Step = 0;
		int32 Problems = 0;
		int32 ServerCountMark = 0;
		double WorldTimeMark = 0.0;
		bool bFilterWas = true;
	};

	AFPSRLPlayerController* LocalPC(UGameInstance* GI)
	{
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		return World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
	}
}

// Test strings for the profanity filter are stored encoded so no offensive text is readable in the source.
static FString Decoded(const TCHAR* Encoded)
{
	FString Out;
	FBase64::Decode(FString(Encoded), Out);
	return Out;
}

static FAutoConsoleCommandWithWorld GFPSRLChatTestCommand(TEXT("FPSRL.ChatTest"),
	TEXT("Test: session text chat input, validation, rate limit, fading and history caps ([ChatTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLChatTest::FState> S = MakeShared<FPSRLChatTest::FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			AFPSRLPlayerController* PC = FPSRLChatTest::LocalPC(GI);
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			UFPSRLChatSubsystem* Chat = UFPSRLChatSubsystem::Get(World);
			UFPSRLChatWidget* Widget = PC ? PC->GetChatWidget() : nullptr;
			if (!PC || !PC->GetPawn() || !Chat || !Widget || !World->GetGameState())
			{
				return ++S->Step < 400;
			}
			auto Check = [S](bool bOk, const FString& What)
			{
				S->Problems += bOk ? 0 : 1;
				UE_LOG(LogFPSRL, Log, TEXT("[ChatTest] %s %s"), bOk ? TEXT("OK     ") : TEXT("PROBLEM"), *What);
			};
			UFPSRLChatSettings* Settings = GetMutableDefault<UFPSRLChatSettings>();
			const TArray<FFPSRLChatMessage>& History = Chat->GetClientHistory();
			const FString Me = PC->PlayerState ? PC->PlayerState->GetPlayerName() : FString();

			++S->Step;
			if (S->Step < 1000)
			{
				S->Step = 1000;
				Chat->ResetSession(TEXT("ChatTest"));
				// Press the open-chat action as the player would (T / D-pad Left are mapped to it).
				const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
				UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
				UInputAction* Action = Settings->OpenChatAction.LoadSynchronous();
				Check(Action && Input, TEXT("open-chat action and input subsystem found"));
				if (Action && Input)
				{
					Input->InjectInputForAction(Action, FInputActionValue(true), {}, {});
				}
				S->WorldTimeMark = World->GetTimeSeconds();
				return true;
			}
			switch (S->Step)
			{
			case 1003:
				Check(PC->IsChatOpen(), FString::Printf(TEXT("open-chat pressed: %s (expect open)"), *Widget->DescribeForTest()));
				Check(Widget->GetInputBox() && Widget->GetInputBox()->HasKeyboardFocus(), TEXT("the text box has keyboard focus"));
				Check(!World->IsPaused() && World->GetTimeSeconds() > S->WorldTimeMark, FString::Printf(TEXT("the game keeps running: paused %d, %.2f s passed"), World->IsPaused(), World->GetTimeSeconds() - S->WorldTimeMark));
				Widget->SetDraftForTest(TEXT("Behind you!"));
				if (FApp::CanEverRender())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: the open chat
				}
				break;
			case 1004:
				Widget->SubmitForTest();	// Enter
				break;
			case 1006:
				Check(History.Num() == 1 && History[0].Text == TEXT("Behind you!") && History[0].DisplayName == Me && History[0].MessageId == 1,
					FString::Printf(TEXT("Enter sent it: %d message(s), first '%s: %s' #%d"), History.Num(), History.IsEmpty() ? TEXT("") : *History[0].DisplayName, History.IsEmpty() ? TEXT("") : *History[0].Text, History.IsEmpty() ? 0 : History[0].MessageId));
				Check(!PC->IsChatOpen() && Widget->DescribeForTest().Contains(TEXT("1 shown")), FString::Printf(TEXT("back to the game, the line on the HUD: %s"), *Widget->DescribeForTest()));
				if (FApp::CanEverRender())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: the compact chat
				}
				S->ServerCountMark = Chat->GetServerHistory().Num();
				PC->ServerSendChatMessage(TEXT("Got it"));
				PC->ServerSendChatMessage(TEXT("spam"));	// inside the cooldown
				break;
			case 1009:
				Check(Chat->GetServerHistory().Num() == S->ServerCountMark && Widget->DescribeForTest().Contains(TEXT("Slow down")),
					FString::Printf(TEXT("two messages inside the %.2f s cooldown: server history %d -> %d; %s (expect both refused, a notice)"), Settings->ChatMessageCooldown, S->ServerCountMark, Chat->GetServerHistory().Num(), *Widget->DescribeForTest()));
				break;
			case 1013:	// a second later
				S->ServerCountMark = Chat->GetServerHistory().Num();
				PC->ServerSendChatMessage(FString::ChrN(Settings->MaxMessageCharacters + 1, TEXT('a')));
				PC->ServerSendChatMessage(TEXT("   \t "));
				break;
			case 1015:
				Check(Chat->GetServerHistory().Num() == S->ServerCountMark, FString::Printf(TEXT("%d characters and whitespace only: server history %d -> %d (expect both refused)"), Settings->MaxMessageCharacters + 1, S->ServerCountMark, Chat->GetServerHistory().Num()));
				// A bell, a line break and a right-to-left override (built from codes, never written raw in source).
				PC->ServerSendChatMessage(FString::Printf(TEXT("Portal%c is\r\nopen%c now "), static_cast<TCHAR>(0x07), static_cast<TCHAR>(0x202E)));
				break;
			case 1017:
				Check(!History.IsEmpty() && History.Last().Text == TEXT("Portal is open now"), FString::Printf(TEXT("control and bidi characters removed: '%s'"), History.IsEmpty() ? TEXT("") : *History.Last().Text));
				// Esc: closes, sends nothing, keeps the draft.
				S->ServerCountMark = Chat->GetServerHistory().Num();
				PC->OpenChat();
				break;
			case 1019:
				Widget->SetDraftForTest(TEXT("half a thought"));
				Widget->CancelForTest();
				break;
			case 1021:
				Check(!PC->IsChatOpen() && Chat->GetServerHistory().Num() == S->ServerCountMark && Widget->DescribeForTest().Contains(TEXT("input 'half a thought'")),
					FString::Printf(TEXT("Esc: %s; server history %d -> %d (expect closed, nothing sent, draft kept)"), *Widget->DescribeForTest(), S->ServerCountMark, Chat->GetServerHistory().Num()));
				Widget->SetDraftForTest(FString::ChrN(Settings->MaxMessageCharacters + 40, TEXT('b')));
				Check(Widget->DescribeForTest().Contains(FString::Printf(TEXT("input '%s'"), *FString::ChrN(Settings->MaxMessageCharacters, TEXT('b')))), FString::Printf(TEXT("typing past %d characters stops at the limit"), Settings->MaxMessageCharacters));
				Widget->SetDraftForTest(FString());
				break;
			case 1060:	// ~10 s after the last message: the compact lines have faded, the history has not
				Check(Widget->DescribeForTest().Contains(TEXT(", 0 shown")) && History.Num() == 2,
					FString::Printf(TEXT("after %.0f s + %.0f s fade: %s; history %d (expect nothing shown, 2 kept)"), Settings->MessageFadeDelay, Settings->MessageFadeDuration, *Widget->DescribeForTest(), History.Num()));
				PC->OpenChat();
				break;
			case 1062:
				Check(Widget->DescribeForTest().StartsWith(TEXT("open | 2 line(s), 2 shown")), FString::Printf(TEXT("opening shows the faded history again: %s"), *Widget->DescribeForTest()));
				PC->CloseChat();
				// History caps: the server keeps MaxServerChatHistory, each player MaxClientChatHistory.
				Settings->ChatMessageCooldown = 0.f;
				for (int32 Index = 0; Index < Settings->MaxServerChatHistory + 10; ++Index)
				{
					Chat->ServerSubmit(PC, FString::Printf(TEXT("message %d"), Index));
				}
				break;
			case 1066:
			{
				Settings->ChatMessageCooldown = 0.75f;
				const TArray<FFPSRLChatMessage>& Server = Chat->GetServerHistory();
				bool bOrdered = true;
				for (int32 Index = 1; Index < History.Num(); ++Index)
				{
					bOrdered &= History[Index].MessageId == History[Index - 1].MessageId + 1;
				}
				Check(Server.Num() == Settings->MaxServerChatHistory && History.Num() == Settings->MaxClientChatHistory && bOrdered && History.Last().Text == FString::Printf(TEXT("message %d"), Settings->MaxServerChatHistory + 9),
					FString::Printf(TEXT("%d more messages: server history %d (cap %d), player history %d (cap %d), ids in order %d, newest '%s'"), Settings->MaxServerChatHistory + 10, Server.Num(), Settings->MaxServerChatHistory,
						History.Num(), Settings->MaxClientChatHistory, bOrdered, *History.Last().Text));
				Check(Widget->DescribeForTest().Contains(FString::Printf(TEXT("%d line(s), %d shown"), Settings->MaxClientChatHistory, Settings->MaxVisibleMessages)),
					FString::Printf(TEXT("compact chat shows the newest %d: %s"), Settings->MaxVisibleMessages, *Widget->DescribeForTest()));
				// Profanity: masked for display when this player's setting is on; the history keeps the original.
				S->bFilterWas = UFPSRLUserSettings::Get()->bFilterProfanity;
				UFPSRLUserSettings::SetFilterProfanity(true);
				Settings->ChatMessageCooldown = 0.f;
				Chat->ServerSubmit(PC, Decoded(TEXT("d2hhdCB0aGUgZnVjayBpcyB0aGlzIHNoaXQ=")));
				break;
			}
			case 1068:
			{
				const FString Shown = Widget->DescribeForTest();
				Check(Shown.Contains(TEXT("'what the **** is this ****'")) && History.Last().Text == Decoded(TEXT("d2hhdCB0aGUgZnVjayBpcyB0aGlzIHNoaXQ=")),
					FString::Printf(TEXT("filter on: %s; history keeps '%s'"), *Shown, *History.Last().Text));
				struct FCase { FString In; FString Out; };
				const FCase Cases[] = {
					{ Decoded(TEXT("Ri5VLkMuSyBvZmY=")), Decoded(TEXT("KioqKioqKiBvZmY=")) }, { Decoded(TEXT("c2ghdA==")), Decoded(TEXT("KioqKg==")) }, { Decoded(TEXT("c2gxdCBoYXBwZW5z")), Decoded(TEXT("KioqKiBoYXBwZW5z")) }, { Decoded(TEXT("ZnV1dXVjaw==")), Decoded(TEXT("KioqKioqKg==")) },
					{ Decoded(TEXT("ZnVja2luZyBoZWxs")), Decoded(TEXT("KioqKioqKiBoZWxs")) }, { Decoded(TEXT("YW4gYXNzaXN0IGluIGNsYXNzIHRvIHBhc3M=")), Decoded(TEXT("YW4gYXNzaXN0IGluIGNsYXNzIHRvIHBhc3M=")) },
					{ Decoded(TEXT("U2hpaXRha2UsIGNvY2twaXQsIHNjdW50aG9ycGU=")), Decoded(TEXT("U2hpaXRha2UsIGNvY2twaXQsIHNjdW50aG9ycGU=")) }, { Decoded(TEXT("eW91IGFzcyE=")), Decoded(TEXT("eW91ICoqKiE=")) },
					{ Decoded(TEXT("d3RmIGZtbCBzdGZ1")), Decoded(TEXT("d3RmIGZtbCBzdGZ1")) },
					{ Decoded(TEXT("bmFnZ2E=")), Decoded(TEXT("KioqKio=")) }, { Decoded(TEXT("bWlnZ2VyIHJpZ2dlciBnaWdnZXI=")), Decoded(TEXT("KioqKioqICoqKioqKiAqKioqKio=")) },
					{ Decoded(TEXT("bjFnZzNyIG4hOTlAIG5sNjZlcg==")), Decoded(TEXT("KioqKioqICoqKioqICoqKioqKg==")) }, { Decoded(TEXT("bi5pLmcuZy5lLnI=")), Decoded(TEXT("KioqKioqKioqKio=")) },
					{ Decoded(TEXT("TnxHR0A=")), Decoded(TEXT("KioqKio=")) }, { Decoded(TEXT("dHJpZ2dlciBiaWdnZXIgZGlnZ2Vy")), Decoded(TEXT("dHJpZ2dlciBiaWdnZXIgZGlnZ2Vy")) },
					{ Decoded(TEXT("TmlhZ2FyYSBOaWdlciBuMWdlciBOSUdFUlM=")), Decoded(TEXT("TmlhZ2FyYSAqKioqKiAqKioqKiAqKioqKio=")) }, { Decoded(TEXT("TmlnZXJpYSBOaWdlcmlhbg==")), Decoded(TEXT("TmlnZXJpYSBOaWdlcmlhbg==")) },
					{ Decoded(TEXT("bnhnZ2VyIG5pWGdlciBuaWdaZXIgbmlnZyNyIG5pZ2dlcQ==")), Decoded(TEXT("KioqKioqICoqKioqKiAqKioqKiogKioqKioqICoqKioqKg==")) }, { Decoded(TEXT("YWJjbmlnZzNyeHl6")), Decoded(TEXT("KioqKioqKioqKioq")) },
					{ Decoded(TEXT("bnVnZ2V0IG5pZ2dsZSBudW1iZXIgZGlubmVy")), Decoded(TEXT("bnVnZ2V0IG5pZ2dsZSBudW1iZXIgZGlubmVy")) },
					{ Decoded(TEXT("bmVncm8gTjNHUjAgbi5lLmcuci5vIG5lZ3JvZXMgbmV4cm8gbmVnciM=")), Decoded(TEXT("KioqKiogKioqKiogKioqKioqKioqICoqKioqKiogKioqKiogKioqKio=")) },
					{ Decoded(TEXT("bmVjcm9tYW5jZXIgbmV1cm9uIG5pdHJvIG1ldHJv")), Decoded(TEXT("bmVjcm9tYW5jZXIgbmV1cm9uIG5pdHJvIG1ldHJv")) },
					{ Decoded(TEXT("eW91IGdvb2ssIGFuIGFuYWwgcGlyYXRlIQ==")), Decoded(TEXT("eW91ICoqKiosIGFuICoqKiogKioqKioqIQ==")) }, { Decoded(TEXT("c3Bvb2t5IGFuYWx5c2lzIG9mIHRoZSBwaXJhdGUgc2hpcA==")), Decoded(TEXT("c3Bvb2t5IGFuYWx5c2lzIG9mIHRoZSBwaXJhdGUgc2hpcA==")) },
					{ Decoded(TEXT("anVzdCBreXMgbG9s")), Decoded(TEXT("anVzdCAqKiogbG9s")) }, { Decoded(TEXT("Z28ga2lsbCB5b3Vyc2VsZiBub3c=")), Decoded(TEXT("KiogKioqKiAqKioqKioqKiBub3c=")) }, { Decoded(TEXT("a2lsbCB0aGUgYm9zcyB5b3Vyc2VsZg==")), Decoded(TEXT("a2lsbCB0aGUgYm9zcyB5b3Vyc2VsZg==")) } };
				for (const FCase& Case : Cases)
				{
					const FString Masked = FPSRLProfanity::Mask(Case.In);
					Check(Masked == Case.Out, FString::Printf(TEXT("mask '%s' -> '%s' (expect '%s')"), *Case.In, *Masked, *Case.Out));
				}
				// The pause menu's Settings page turns it off, as a player would.
				PC->OpenPauseMenu();
				break;
			}
			case 1070:
			{
				UFPSRLPauseMenuWidget* Pause = PC->GetPauseMenu();
				if (Pause)
				{
					Pause->ShowSettingsPage(true);
				}
				Check(Pause && Pause->IsSettingsPageShown() && Pause->GetProfanityCheck() && Pause->GetProfanityCheck()->IsChecked(),
					TEXT("pause > Settings shows the profanity box, ticked (filter on)"));
				if (FApp::CanEverRender())
				{
					PC->ConsoleCommand(TEXT("Shot showui"));	// rendered runs: the Settings page
				}
				if (Pause && Pause->GetProfanityCheck())
				{
					Pause->GetProfanityCheck()->SetIsChecked(false);
					Pause->GetProfanityCheck()->OnCheckStateChanged.Broadcast(false);	// the click
				}
				break;
			}
			case 1072:
			{
				const FString Shown = Widget->DescribeForTest();
				Check(!UFPSRLUserSettings::Get()->bFilterProfanity && Shown.Contains(Decoded(TEXT("J3doYXQgdGhlIGZ1Y2sgaXMgdGhpcyBzaGl0Jw=="))), FString::Printf(TEXT("unticked: setting off, chat redrawn: %s"), *Shown));
				PC->ClosePauseMenu();
				UFPSRLUserSettings::SetFilterProfanity(S->bFilterWas);	// leave the player's setting as it was
				Settings->ChatMessageCooldown = 0.75f;
				UE_LOG(LogFPSRL, Log, TEXT("[ChatTest] state: %s"), *Chat->Describe(World));
				UE_LOG(LogFPSRL, Log, TEXT("[ChatTest] done: %d problem(s)"), S->Problems);
				PC->ConsoleCommand(TEXT("quit"));
				return false;
			}
			default:
				break;
			}
			return true;
		}), 0.25f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRLChatSayCommand(TEXT("fpsrl.Chat.Say"),
	TEXT("Send a chat message as the local player: fpsrl.Chat.Say <text>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFPSRLPlayerController* PC = World ? Cast<AFPSRLPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController(World)) : nullptr;
		if (PC)
		{
			PC->ServerSendChatMessage(FString::Join(Args, TEXT(" ")));
		}
	}));

namespace FPSRLChatTest
{
	FTSTicker::FDelegateHandle AutoSayHandle;
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLChatAutoSayCommand(TEXT("fpsrl.Chat.AutoSay"),
	TEXT("Keep sending numbered chat messages every N seconds as the local player (survives level travel); 0 stops."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FPSRLChatTest::AutoSayHandle);
		const float Interval = Args.IsEmpty() ? 0.f : FCString::Atof(*Args[0]);
		TWeakObjectPtr<UGameInstance> GameInstance = World ? World->GetGameInstance() : nullptr;
		if (Interval <= 0.f || !GameInstance.IsValid())
		{
			return;
		}
		TSharedRef<int32> Count = MakeShared<int32>(0);
		FPSRLChatTest::AutoSayHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, Count](float)
		{
			AFPSRLPlayerController* PC = FPSRLChatTest::LocalPC(GameInstance.Get());
			if (PC && PC->PlayerState && (PC->HasAuthority() || PC->GetNetConnection()))
			{
				PC->ServerSendChatMessage(FString::Printf(TEXT("auto %d from %s on %s"), ++*Count, *PC->PlayerState->GetPlayerName(), *PC->GetWorld()->GetMapName()));
			}
			return GameInstance.IsValid();
		}), Interval);
	}));
#endif
