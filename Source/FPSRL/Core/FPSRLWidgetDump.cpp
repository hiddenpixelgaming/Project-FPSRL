// Fill out your copyright notice in the Description page of Project Settings.

// Debug only: fpsrl.Debug.Widgets [delay seconds] logs every user widget on screen (class, visibility, opacity, scale,
// canvas position) and its children: finds HUD pieces that exist but don't show. The delay lets it run after a travel.

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLWidgetDump
{
	FString Describe(const UWidget* W)
	{
		const FVector2D Scale = W->GetRenderTransform().Scale;
		const UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot);
		const FString Where = Slot ? FString::Printf(TEXT(", canvas pos %s size %s"), *Slot->GetPosition().ToString(), *Slot->GetSize().ToString()) : FString();
		return FString::Printf(TEXT("%s (%s) visibility %s, visible now %d, opacity %.2f, scale %.2f x %.2f, desired %s"),
			*W->GetName(), *W->GetClass()->GetName(), *StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)W->GetVisibility()),
			W->IsVisible() ? 1 : 0, W->GetRenderOpacity(), Scale.X, Scale.Y, *W->GetDesiredSize().ToString()) + Where;
	}

	void Dump(UWorld* World)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Widgets] in %s"), *GetNameSafe(World));
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			if (It->GetWorld() != World || It->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			UE_LOG(LogFPSRL, Log, TEXT("[Widgets] %s%s"), It->IsInViewport() ? TEXT("") : TEXT("(NOT ON SCREEN) "), *Describe(*It));
			if (It->WidgetTree && It->IsInViewport())
			{
				It->WidgetTree->ForEachWidget([](UWidget* Child)
				{
					UE_LOG(LogFPSRL, Log, TEXT("[Widgets]     %s"), *Describe(Child));
				});
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLWidgetDumpCommand(TEXT("fpsrl.Debug.Widgets"),
	TEXT("fpsrl.Debug.Widgets [delay seconds]: logs every user widget and its children ([Widgets])."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		if (Delay <= 0.f)
		{
			FPSRLWidgetDump::Dump(World);
			return;
		}
		TWeakObjectPtr<UGameInstance> GameInstance = World ? World->GetGameInstance() : nullptr;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance](float)
		{
			if (UGameInstance* GI = GameInstance.Get())
			{
				FPSRLWidgetDump::Dump(GI->GetWorld());
			}
			return false;
		}), Delay);
	}));
#endif
