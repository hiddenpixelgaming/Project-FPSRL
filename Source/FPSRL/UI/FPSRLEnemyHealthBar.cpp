// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLEnemyHealthBar.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Types/FPSRLGameplayTags.h"

namespace FPSRLEnemyHealthBars
{
	static TAutoConsoleVariable<int32> CVarMode(TEXT("fpsrl.EnemyHealthBars"), 1,
		TEXT("Overhead health bars on ordinary enemies: 0 off, 1 once damaged, 2 always (Minibosses and bosses never)."));

	const FVector2D Size(90.f, 7.f);
	const float HeadClearance = 30.f;	// cm above the top of the capsule
}

void UFPSRLEnemyHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	// A dark back one pixel wider than the fill, so the bar reads against any background.
	UBorder* Back = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Back"));
	Back->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Back->SetPadding(FMargin(1.f));
	WidgetTree->RootWidget = Back;

	Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar"));
	FProgressBarStyle Style = Bar->GetWidgetStyle();
	Style.BackgroundImage.TintColor = FSlateColor(FLinearColor(0.12f, 0.02f, 0.02f, 0.85f));
	Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
	Bar->SetWidgetStyle(Style);
	Bar->SetFillColorAndOpacity(FLinearColor(0.85f, 0.08f, 0.06f));
	Bar->SetPercent(1.f);
	Back->SetContent(Bar);
}

void UFPSRLEnemyHealthBarWidget::SetFraction(float Fraction)
{
	if (Bar)
	{
		Bar->SetPercent(FMath::Clamp(Fraction, 0.f, 1.f));
	}
}

float UFPSRLEnemyHealthBarWidget::GetFraction() const
{
	return Bar ? Bar->GetPercent() : -1.f;
}

UFPSRLEnemyHealthBarComponent::UFPSRLEnemyHealthBarComponent()
{
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawSize(FPSRLEnemyHealthBars::Size);
	SetPivot(FVector2D(0.5f, 1.f));
	SetWidgetClass(UFPSRLEnemyHealthBarWidget::StaticClass());
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CanCharacterStepUpOn = ECB_No;
	SetCastShadow(false);
	PrimaryComponentTick.bStartWithTickEnabled = false;	// ticks only while shown (see Refresh)
	SetHiddenInGame(true);
}

UFPSRLEnemyHealthBarComponent* UFPSRLEnemyHealthBarComponent::AddTo(APawn* Enemy, UFPSRLHealthComponent* InHealth, UAbilitySystemComponent* ASC)
{
	if (!Enemy || !InHealth || Enemy->GetNetMode() == NM_DedicatedServer || !Enemy->GetRootComponent())
	{
		return nullptr;
	}
	UFPSRLEnemyHealthBarComponent* Component = NewObject<UFPSRLEnemyHealthBarComponent>(Enemy, TEXT("OverheadHealthBar"));
	const ACharacter* Character = Cast<ACharacter>(Enemy);
	const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
	Component->SetupAttachment(Enemy->GetRootComponent());
	Component->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + FPSRLEnemyHealthBars::HeadClearance));
	Component->RegisterComponent();
	Component->Health = InHealth;
	Component->AbilitySystem = ASC;
	InHealth->OnHealthChanged.AddDynamic(Component, &ThisClass::HandleHealthChanged);
	if (ASC)
	{
		// The rank tag replicates after the enemy appears on clients: hide the bar as soon as it arrives.
		Component->MinibossHandle = ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Enemy_Rank_Miniboss).AddUObject(Component, &ThisClass::HandleRankTagChanged);
		Component->BossHandle = ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Enemy_Rank_Boss).AddUObject(Component, &ThisClass::HandleRankTagChanged);
	}
	Component->Refresh();
	return Component;
}

void UFPSRLEnemyHealthBarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Enemy_Rank_Miniboss).Remove(MinibossHandle);
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Enemy_Rank_Boss).Remove(BossHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UFPSRLEnemyHealthBarComponent::HandleHealthChanged(double CurrentHealth, double MaxHealth)
{
	Refresh();
}

void UFPSRLEnemyHealthBarComponent::HandleRankTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	Refresh();
}

void UFPSRLEnemyHealthBarComponent::Refresh()
{
	const UFPSRLHealthComponent* HealthComponent = Health.Get();
	const UAbilitySystemComponent* ASC = AbilitySystem.Get();
	const float Current = HealthComponent ? HealthComponent->GetCurrentHealth() : 0.f;
	const float Max = HealthComponent ? HealthComponent->GetMaxHealth() : 0.f;
	const bool bRanked = ASC && (ASC->HasMatchingGameplayTag(FPSRLGameplayTags::Enemy_Rank_Miniboss) || ASC->HasMatchingGameplayTag(FPSRLGameplayTags::Enemy_Rank_Boss));
	const int32 Mode = FPSRLEnemyHealthBars::CVarMode.GetValueOnGameThread();

	bShown = Mode > 0 && !bRanked && Max > 0.f && Current > 0.f && (Mode >= 2 || Current < Max);
	if (UFPSRLEnemyHealthBarWidget* Bar = Cast<UFPSRLEnemyHealthBarWidget>(GetWidget()))
	{
		Bar->SetFraction(Max > 0.f ? Current / Max : 0.f);
	}
	SetHiddenInGame(!bShown);
	SetComponentTickEnabled(bShown);	// a screen-space widget follows its enemy through the tick: only while visible
}

float UFPSRLEnemyHealthBarComponent::GetShownFraction() const
{
	const UFPSRLEnemyHealthBarWidget* Bar = Cast<UFPSRLEnemyHealthBarWidget>(GetWidget());
	return Bar ? Bar->GetFraction() : -1.f;
}
