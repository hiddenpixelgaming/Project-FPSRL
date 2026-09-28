// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FPSRLMenuStatusWidget.generated.h"

class UTextBlock;

/** One line of status on the Menu ("Creating game...", or why hosting failed). Default layout built in code. */
UCLASS()
class FPSRL_API UFPSRLMenuStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStatus(const FText& Message, bool bError);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(BlueprintReadOnly, Category = "Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;
};
