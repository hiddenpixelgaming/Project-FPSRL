// Fill out your copyright notice in the Description page of Project Settings.

// Test only:
//  - FPSRL.ChatTest (solo, any level with a pawn): the open-chat action opens the input with focus and the game keeps
//    running; Enter sends; the server rate-limits, refuses empty / too long text and strips control characters;
//    Esc closes without sending and keeps the draft; compact lines fade but stay in the history; both histories cap.
//  - fpsrl.Chat.Say <text>: send a chat message as the local player (the normal client path).
//  - fpsrl.Chat.AutoSay <seconds>: keep sending numbered messages (multiplayer and level-travel checks); 0 stops.

#include "Containers/Ticker.h"
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
	};

	AFPSRLPlayerController* LocalPC(UGameInstance* GI)
	{
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		return World ? Cast<AFPSRLPlayerController>(GI->GetFirstLocalPlayerController(World)) : nullptr;
	}
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
