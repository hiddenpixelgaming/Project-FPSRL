// Fill out your copyright notice in the Description page of Project Settings.

#include "Social/FPSRLProfanityFilter.h"
#include "Internationalization/Regex.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Social/FPSRLChatSettings.h"

namespace FPSRLProfanity
{
	/** Lowercase letters only, with look-alike digits and symbols read as letters. */
	static FString Normalize(const FString& Word)
	{
		FString Out;
		Out.Reserve(Word.Len());
		for (TCHAR C : Word)
		{
			switch (C)
			{
			case TEXT('0'): C = TEXT('o'); break;
			case TEXT('1'): case TEXT('!'): case TEXT('|'): C = TEXT('i'); break;
			case TEXT('3'): C = TEXT('e'); break;
			case TEXT('4'): case TEXT('@'): C = TEXT('a'); break;
			case TEXT('5'): case TEXT('$'): C = TEXT('s'); break;
			case TEXT('7'): case TEXT('+'): C = TEXT('t'); break;
			default: break;
			}
			if (FChar::IsAlpha(C))
			{
				Out.AppendChar(FChar::ToLower(C));
			}
		}
		return Out;
	}

	/** Runs of one letter squeezed to one (e.g. "heeelllo" -> "helo"). */
	static FString Squeeze(const FString& Word)
	{
		FString Out;
		for (const TCHAR C : Word)
		{
			if (Out.IsEmpty() || Out[Out.Len() - 1] != C)
			{
				Out.AppendChar(C);
			}
		}
		return Out;
	}

	/** A list entry against a normalized word: whole word, or prefix for entries ending in '*'. */
	static bool Matches(const FString& Word, const FString& Entry)
	{
		const bool bPrefix = Entry.EndsWith(TEXT("*"));
		const FString Root = Normalize(bPrefix ? Entry.LeftChop(1) : Entry);
		if (Root.IsEmpty())
		{
			return false;
		}
		if (bPrefix ? Word.StartsWith(Root) : Word == Root)
		{
			return true;
		}
		// Stretched letters: the squeezed word must equal the squeezed root exactly, only when the typed word
		// really had repeats (so a short word never matches a longer listed word) and the root is long enough to be unambiguous. Never as a
		// prefix, so ordinary words with doubled letters are not caught.
		const FString Squeezed = Squeeze(Word);
		return Squeezed != Word && Root.Len() >= 4 && Squeezed == Squeeze(Root);
	}

	/** Base64 text to a plain string (the rules below keep their words encoded so none are readable in the source). */
	static FString Decode(const TCHAR* Encoded)
	{
		FString Out;
		FBase64::Decode(FString(Encoded), Out);
		return Out;
	}

	/**
	 * The racial slur rules, checked on the raw word (lowercased, separators removed) so look-alike characters count
	 * (each letter may be written as a digit or symbol that resembles it). Three patterns and two one-character-off
	 * templates, all stored encoded. Words on the allow list are let through.
	 */
	static bool IsSlurVariant(const FString& RawWord)
	{
		FString Compact;
		for (const TCHAR C : RawWord.ToLower())
		{
			if (!FChar::IsWhitespace(C) && C != TEXT('.') && C != TEXT('-') && C != TEXT('_') && C != TEXT('\'') && C != TEXT('*') && C != TEXT('~') && C != TEXT(','))
			{
				Compact.AppendChar(C);
			}
		}
		// Rule A: the word family anywhere in a word; rule B: its short form; rule C: a single-letter variant as a whole
		// word only (so a country's name stays readable).
		static const FRegexPattern PatternA(Decode(TEXT("W2lsMXwhN0A2OXE1Z11bZzY5NTNAcV1bZzY5NTNAcV0rW2UzXSty")));
		static const FRegexPattern PatternB(Decode(TEXT("bltpbDF8ITdANGFlM10rW2c2OTUzQHFdW2c2OTUzQHFdKyhhfEB8NHxhaHx1aCk=")));
		static const FRegexPattern PatternC(Decode(TEXT("Xm4rW2lsMXwhN0BdK1tnNjk1M0BxXStbZTNdK3Ircz8k")));
		FRegexMatcher MatchA(PatternA, Compact);
		FRegexMatcher MatchB(PatternB, Compact);
		FRegexMatcher MatchC(PatternC, Compact);

		// Templates D and E: "n" + the template with at most ONE character different (that one may be anything), anywhere
		// in the word (user: any variation, any format). Look-alikes count as the letter.
		auto IsLike = [](TCHAR C, TCHAR Letter)
		{
			switch (Letter)
			{
			case TEXT('i'): return FCString::Strchr(TEXT("il1|!7@"), C) != nullptr;
			case TEXT('g'): return FCString::Strchr(TEXT("g6953@q"), C) != nullptr;
			case TEXT('e'): return C == TEXT('e') || C == TEXT('3');
			case TEXT('o'): return C == TEXT('o') || C == TEXT('0');
			default: return C == Letter;
			}
		};
		auto OneOffAnywhere = [&Compact, &IsLike](const FString& RestString)
		{
			const TCHAR* Rest = *RestString;
			const int32 RestLen = FCString::Strlen(Rest);
			for (int32 At = 0; At + 1 + RestLen <= Compact.Len(); ++At)
			{
				if (Compact[At] != TEXT('n'))
				{
					continue;
				}
				int32 Mismatches = 0;
				for (int32 Offset = 0; Offset < RestLen; ++Offset)
				{
					Mismatches += IsLike(Compact[At + 1 + Offset], Rest[Offset]) ? 0 : 1;
				}
				if (Mismatches <= 1)
				{
					return true;
				}
			}
			return false;
		};
		static const FString TemplateD = Decode(TEXT("aWdnZXI="));
		static const FString TemplateE = Decode(TEXT("ZWdybw=="));
		const bool bOneOff = OneOffAnywhere(TemplateD) || OneOffAnywhere(TemplateE);
		if (!bOneOff && !MatchA.FindNext() && !MatchB.FindNext() && !MatchC.FindNext())
		{
			return false;
		}
		const FString Plain = Normalize(Compact);
		for (const FString& Allowed : UFPSRLChatSettings::Get()->ProfanityAllowWords)
		{
			if (Matches(Plain, Allowed))
			{
				return false;
			}
		}
		return true;
	}

	/** The published list (Content/Moderation/ProfanityList.txt), loaded once: single words as a set, phrases as word lists. */
	struct FListedWords
	{
		TSet<FString> Words;
		TArray<FString> Prefixes;	// entries ending in '*': also longer words starting with them
		TArray<TArray<FString>> Phrases;
	};

	static const FListedWords& GetListedWords()
	{
		static FListedWords Listed;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			FString File;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Moderation/ProfanityList.txt");
			if (FFileHelper::LoadFileToString(File, *Path))
			{
				TArray<FString> Lines;
				File.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
					{
						continue;
					}
					// Each entry is base64 (no readable words in the repository).
					FString Entry;
					if (!FBase64::Decode(Line.TrimStartAndEnd(), Entry) || Entry.IsEmpty())
					{
						continue;
					}
					if (!Entry.Contains(TEXT(" ")) && Entry.EndsWith(TEXT("*")))
					{
						Listed.Prefixes.Add(Entry);
						continue;
					}
					TArray<FString> Parts;
					Entry.ParseIntoArrayWS(Parts);
					TArray<FString> Normalized;
					for (const FString& Part : Parts)
					{
						if (const FString Word = Normalize(Part); !Word.IsEmpty())
						{
							Normalized.Add(Word);
						}
					}
					if (Normalized.Num() == 1)
					{
						Listed.Words.Add(Normalized[0]);
					}
					else if (Normalized.Num() > 1)
					{
						Listed.Phrases.Add(MoveTemp(Normalized));
					}
				}
			}
			UE_LOG(LogTemp, Log, TEXT("[Chat] Profanity list: %d words, %d prefixes, %d phrases (%s)"), Listed.Words.Num(), Listed.Prefixes.Num(), Listed.Phrases.Num(), *Path);
		}
		return Listed;
	}

	FString Mask(const FString& Text)
	{
		const TArray<FString>& Entries = UFPSRLChatSettings::Get()->ProfanityWords;
		const FListedWords& Listed = GetListedWords();
		struct FToken { int32 Start, End, WordStart, WordEnd; FString Word; bool bDecided; };	// bDecided: masked, or kept readable, on its own
		TArray<FToken> Tokens;
		FString Out = Text;
		int32 Start = 0;
		while (Start < Out.Len())
		{
			// One word = a run of non-space characters.
			while (Start < Out.Len() && FChar::IsWhitespace(Out[Start]))
			{
				++Start;
			}
			int32 End = Start;
			while (End < Out.Len() && !FChar::IsWhitespace(Out[End]))
			{
				++End;
			}
			// Punctuation at the ends of a word is not part of it (trailing "!", wrapping brackets); '@', '$', '|', '!' at the start stay
			// (they stand in for letters).
			auto IsEdge = [](TCHAR C) { return FChar::IsPunct(C) && C != TEXT('@') && C != TEXT('$') && C != TEXT('|'); };
			int32 WordStart = Start;
			int32 WordEnd = End;
			while (WordStart < WordEnd && IsEdge(Out[WordStart]))
			{
				++WordStart;
			}
			while (WordEnd > WordStart && IsEdge(Out[WordEnd - 1]))
			{
				--WordEnd;
			}
			if (WordEnd > WordStart)
			{
				const FString Raw = Out.Mid(WordStart, WordEnd - WordStart);
				const FString Word = Normalize(Raw);
				// Slurs are checked on the whole run, edge symbols included (one may stand in for a letter),
				// and then the whole run is masked.
				const bool bSlur = IsSlurVariant(Out.Mid(Start, End - Start));
				// Words kept readable on purpose (allow list: "stfu", "trigger"...) beat the word lists, never a slur.
				bool bAllowed = false;
				for (const FString& Allowed : UFPSRLChatSettings::Get()->ProfanityAllowWords)
				{
					bAllowed |= !Word.IsEmpty() && Matches(Word, Allowed);
				}
				bool bMask = bSlur || (!bAllowed && !Word.IsEmpty() && Listed.Words.Contains(Word));
				for (int32 Index = 0; !bMask && !bAllowed && !Word.IsEmpty() && Index < Listed.Prefixes.Num(); ++Index)
				{
					bMask = Matches(Word, Listed.Prefixes[Index]);
				}
				for (int32 Index = 0; !bMask && !bAllowed && !Word.IsEmpty() && Index < Entries.Num(); ++Index)
				{
					bMask = Matches(Word, Entries[Index]);
				}
				Tokens.Add({ Start, End, WordStart, WordEnd, Word, bMask || bAllowed });
				if (bMask)
				{
					for (int32 Index = bSlur ? Start : WordStart; Index < (bSlur ? End : WordEnd); ++Index)
					{
						Out[Index] = TEXT('*');	// the whole word, inner symbols too ("sh!t")
					}
				}
			}
			Start = End;
		}

		// Listed phrases: consecutive words matching every word of the phrase are masked (spaces kept).
		for (const TArray<FString>& Phrase : Listed.Phrases)
		{
			for (int32 First = 0; First + Phrase.Num() <= Tokens.Num(); ++First)
			{
				bool bMatch = true;
				// Only phrases made of words that are not offensive on their own ; when a word in it
				// is already masked (masked on its own), the rest stays readable.
				bool bAnyDecided = false;
				for (int32 Offset = 0; bMatch && Offset < Phrase.Num(); ++Offset)
				{
					bMatch = Tokens[First + Offset].Word == Phrase[Offset];
				}
				for (int32 Offset = 0; bMatch && Offset < Phrase.Num(); ++Offset)
				{
					bAnyDecided |= Tokens[First + Offset].bDecided;
				}
				bMatch &= !bAnyDecided;
				if (bMatch)
				{
					for (int32 Offset = 0; Offset < Phrase.Num(); ++Offset)
					{
						const FToken& Token = Tokens[First + Offset];
						for (int32 Index = Token.WordStart; Index < Token.WordEnd; ++Index)
						{
							Out[Index] = TEXT('*');
						}
					}
				}
			}
		}
		return Out;
	}
}
