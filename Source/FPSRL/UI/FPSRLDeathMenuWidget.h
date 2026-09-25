// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLDeathMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * "YOU DIED" screen shown after the fade to black (AFPSRLPlayerController). Presentation only.
 * Return to Lobby is only offered once the whole party is down (co-op players can't leave the host's run alone);
 * until then it shows that the team is still fighting. Builds a default layout in code; a Blueprint subclass may
 * supply its own with widgets named Title, Status, ReturnButton, QuitButton (all optional).
 */
UCLASS()
class FPSRL_API UFPSRLDeathMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** bCanReturn: everyone is down (or solo), so Return to Lobby ends the run. */
	void SetCanReturn(bool bCanReturn);

	FSimpleDelegate OnReturnToLobby;
	FSimpleDelegate OnQuitGame;

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "Death", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(BlueprintReadOnly, Category = "Death", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Status;

	UPROPERTY(BlueprintReadOnly, Category = "Death", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReturnButton;

	UPROPERTY(BlueprintReadOnly, Category = "Death", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton;

private:
	void BuildDefaultLayout();

	UFUNCTION() void HandleReturn();
	UFUNCTION() void HandleQuit();
};
