// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FPSRLChatTypes.generated.h"

/** One chat message, as the server accepted it. Every field except Text is set by the server, never by the sender. */
USTRUCT(BlueprintType)
struct FFPSRLChatMessage
{
	GENERATED_BODY()

	/** Server-assigned, increasing for the whole session (survives level travel): the authoritative order. */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	int32 MessageId = 0;

	/** The sender's PlayerState PlayerId, read on the server. */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	int32 SenderPlayerId = INDEX_NONE;

	/** The sender's PlayerState name (Steam name), read on the server. */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString DisplayName;

	/** The sanitised message text: plain text only, never interpreted (no markup, no commands). */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString Text;

	/** Server UTC time the message was accepted (FDateTime ticks). */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	int64 ServerTimestampTicks = 0;

	/** This machine only (never sent): when the message arrived here (platform seconds; 0 = arrived with the join
	 *  history), so the compact chat fades it from its arrival, also across level travel. */
	UPROPERTY(NotReplicated)
	double LocalArrivalSeconds = 0.0;
};

/** Why the server refused a message (told only to its sender). */
UENUM(BlueprintType)
enum class EFPSRLChatRejectReason : uint8
{
	None,
	Disabled,		// chat is switched off for this situation (lobby, expedition, downed, dead, traversal)
	NotInSession,	// the sender isn't a connected member of this session
	Empty,
	TooLong,
	Filtered,		// the filter refused it
	RateLimited
};
