// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Masks profanity for display (the chat widget, when the player's "Filter profanity" setting is on). Words come from
 * Content/Moderation/ProfanityList.txt (encoded) and UFPSRLChatSettings::ProfanityWords: whole words, case-insensitive; an entry ending in '*' also matches longer words
 * starting with it (a prefix entry). Simple disguises are seen through: digit / symbol letters,
 * separators inside a word and stretched letters. A match is replaced by asterisks of the same length.
 */
namespace FPSRLProfanity
{
	FPSRL_API FString Mask(const FString& Text);
}
