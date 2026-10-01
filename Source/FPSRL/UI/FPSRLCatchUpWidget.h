// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLCatchUpWidget.generated.h"

class UTextBlock;

/**
 * The optional catch-up prompt (presentation only: the server decides everything, see UFPSRLDepthLayoutComponent).
 * Upper middle of the screen, never takes input or the cursor, so the player keeps playing: "[T] Catch up  [X] Stay"
 * (the keys are AFPSRLPlayerController's). Builds a default layout in code; a Blueprint subclass may supply its own with
 * widgets named Title and Keys.
 */
UCLASS()
class FPSRL_API UFPSRLCatchUpWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowOffer(const FText& Message, const FText& RoomName);

	/** Hide; with a message it lingers briefly showing it ("Caught up: ..."). */
	void HideOffer(const FText& Message = FText::GetEmpty());

	FText GetShownTitle() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "CatchUp", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "CatchUp", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Keys;

private:
	void HideNow();

	FTimerHandle HideTimer;
};
