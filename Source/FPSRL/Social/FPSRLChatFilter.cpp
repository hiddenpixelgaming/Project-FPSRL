// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLChatFilter.h"

namespace FPSRLChatFilter
{
	/** Characters that could break or spoof the UI: controls, and invisible formatting such as bidi overrides. */
	static bool IsUnsafe(TCHAR C)
	{
		const uint32 Code = static_cast<uint32>(C);
		return Code < 0x20 || (Code >= 0x7F && Code <= 0x9F)		// C0 / C1 controls, DEL
			|| (Code >= 0x200B && Code <= 0x200F)					// zero-width spaces and marks
			|| (Code >= 0x202A && Code <= 0x202E)					// bidi embedding / override
			|| (Code >= 0x2066 && Code <= 0x2069)					// bidi isolates
			|| Code == 0xFEFF || Code == 0x2028 || Code == 0x2029;	// BOM, line / paragraph separators
	}
}

FString UFPSRLChatFilter::Sanitize(const FString& RawText)
{
	FString Out;
	Out.Reserve(RawText.Len());
	bool bLastWasSpace = true;	// also drops leading whitespace
	for (const TCHAR C : RawText)
	{
		const bool bSpace = FChar::IsWhitespace(C) || FPSRLChatFilter::IsUnsafe(C);
		if (bSpace)
		{
			if (!bLastWasSpace)
			{
				Out.AppendChar(TEXT(' '));
			}
		}
		else
		{
			Out.AppendChar(C);
		}
		bLastWasSpace = bSpace;
	}
	Out.TrimEndInline();
	return Out;
}

bool UFPSRLChatFilter::FilterMessage(const FString& RawText, FString& OutText, EFPSRLChatRejectReason& OutReason) const
{
	OutText = Sanitize(RawText);
	OutReason = OutText.IsEmpty() ? EFPSRLChatRejectReason::Empty : EFPSRLChatRejectReason::None;
	return !OutText.IsEmpty();
}
