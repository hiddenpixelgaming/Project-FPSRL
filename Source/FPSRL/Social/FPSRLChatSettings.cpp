// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLChatSettings.h"
#include "Social/FPSRLChatFilter.h"
#include "InputAction.h"
#include "InputMappingContext.h"

UFPSRLChatSettings::UFPSRLChatSettings()
{
	FilterClass = UFPSRLChatFilter::StaticClass();
	OpenChatAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Input/Actions/IA_OpenChat.IA_OpenChat")));
	ChatMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Chat.IMC_Chat")));

	// The word list lives in Content/Moderation/ProfanityList.txt (encoded); ProfanityWords here is for project additions.

	// Words kept readable: ordinary words the slur checks would catch, and abbreviations (user: only full words masked).
	ProfanityAllowWords = { TEXT("bigger"), TEXT("trigger*"), TEXT("digger*"), TEXT("necro*"), TEXT("neuro*"), TEXT("wtf"), TEXT("fml"), TEXT("stfu") };
}
