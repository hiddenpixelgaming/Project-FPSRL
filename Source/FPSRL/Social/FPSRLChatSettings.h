// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRLChatSettings.generated.h"

class UFPSRLChatFilter;
class UInputAction;
class UInputMappingContext;

/**
 * Text chat configuration (Project Settings > FPSRL Chat, saved in DefaultGame.ini). All numbers here are our own
 * choices; the Abyssus reference only tells us chat exists, opens with T (D-pad Left on a controller) and is text only.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSRL Chat"))
class FPSRL_API UFPSRLChatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFPSRLChatSettings();

	static const UFPSRLChatSettings* Get() { return GetDefault<UFPSRLChatSettings>(); }

	/** Longest message the server accepts (characters, after sanitising). The input box also stops at this length. */
	UPROPERTY(Config, EditAnywhere, Category = "Messages", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxMessageCharacters = 160;

	/** Minimum seconds between two accepted messages from one player; faster ones are refused (no other penalty). */
	UPROPERTY(Config, EditAnywhere, Category = "Messages", meta = (ClampMin = "0"))
	float ChatMessageCooldown = 0.75f;

	/** Rolling session history on the server (sent to players who join later). Oldest messages drop first. */
	UPROPERTY(Config, EditAnywhere, Category = "History", meta = (ClampMin = "1"))
	int32 MaxServerChatHistory = 100;

	/** Rolling history each player keeps (shown when the chat is open). Oldest messages drop first. */
	UPROPERTY(Config, EditAnywhere, Category = "History", meta = (ClampMin = "1"))
	int32 MaxClientChatHistory = 50;

	/** Lines shown in the compact (closed) chat. */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "1"))
	int32 MaxVisibleMessages = 8;

	/** Seconds a line stays fully visible in the compact chat before it fades. Fading never deletes it from history. */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "0"))
	float MessageFadeDelay = 8.f;

	/** Seconds the fade itself takes. */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "0.05"))
	float MessageFadeDuration = 1.f;

	/** Keep the input open after sending (else Enter sends and returns to the game). */
	UPROPERTY(Config, EditAnywhere, Category = "Display")
	bool bKeepChatOpenAfterSend = false;

	UPROPERTY(Config, EditAnywhere, Category = "Availability")
	bool bChatEnabledInLobby = true;

	UPROPERTY(Config, EditAnywhere, Category = "Availability")
	bool bChatEnabledInExpedition = true;

	UPROPERTY(Config, EditAnywhere, Category = "Availability")
	bool bChatEnabledWhileDowned = true;

	UPROPERTY(Config, EditAnywhere, Category = "Availability")
	bool bChatEnabledWhileDead = true;

	UPROPERTY(Config, EditAnywhere, Category = "Availability")
	bool bChatEnabledDuringTraversal = true;

	/** Words masked for players with "Filter profanity" on (their own setting; the server sends the original text).
	 *  Whole words, case-insensitive; an entry ending in '*' also matches longer words starting with it. */
	UPROPERTY(Config, EditAnywhere, Category = "Filtering")
	TArray<FString> ProfanityWords;

	/** Words the built-in slur rules would catch but that are ordinary words, plus abbreviations kept readable; these
	 *  are let through ('*' = also longer words starting with it). */
	UPROPERTY(Config, EditAnywhere, Category = "Filtering")
	TArray<FString> ProfanityAllowWords;

	/** The server's filter step (sanitising today; moderation later by swapping the class). */
	UPROPERTY(Config, EditAnywhere, Category = "Filtering")
	TSoftClassPtr<UFPSRLChatFilter> FilterClass;

	/** Open-chat action (T, gamepad D-pad Left) and the mapping context holding it; rebind in the context asset. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputAction> OpenChatAction;

	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> ChatMappingContext;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
