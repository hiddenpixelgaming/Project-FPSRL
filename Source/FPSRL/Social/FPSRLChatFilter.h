// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Social/FPSRLChatTypes.h"
#include "FPSRLChatFilter.generated.h"

/**
 * The replaceable filter step of the server's chat validation. The server runs every submitted message through the
 * filter class named in the chat settings. This default only sanitises: control and invisible formatting characters
 * become spaces, whitespace runs collapse to one space, and the ends are trimmed. A profanity filter, a moderation
 * hook or logging for reports later subclasses this (and is selected in Project Settings > FPSRL Chat); nothing else
 * changes.
 */
UCLASS(Blueprintable)
class FPSRL_API UFPSRLChatFilter : public UObject
{
	GENERATED_BODY()

public:
	/** Server: turn the raw text into what is broadcast. Return false (and a reason) to refuse the message. */
	virtual bool FilterMessage(const FString& RawText, FString& OutText, EFPSRLChatRejectReason& OutReason) const;

	/** The basic sanitising step on its own (also used by the client to keep its input clean). */
	static FString Sanitize(const FString& RawText);
};
