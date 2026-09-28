// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/FPSRLCombatHUDWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/FPSRLWeapon.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/FPSRLPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "FPSRLCombatHUD"

void UFPSRLCombatHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);	// never takes the mouse
}

void UFPSRLCombatHUDWidget::BuildDefaultLayout()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	WidgetTree->RootWidget = Canvas;
	auto MakeText = [this](const TCHAR* Name, int32 Size)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		return Text;
	};

	// Damage flash: full screen, transparent until hit.
	DamageFlash = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DamageFlash"));
	DamageFlash->SetColorAndOpacity(FLinearColor(1.f, 0.f, 0.f, 0.f));
	if (UCanvasPanelSlot* FlashSlot = Canvas->AddChildToCanvas(DamageFlash))
	{
		FlashSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		FlashSlot->SetOffsets(FMargin(0.f));
	}

	// Bottom left: health.
	UVerticalBox* HealthColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HealthColumn"));
	HealthText = MakeText(TEXT("HealthText"), 16);
	HealthColumn->AddChildToVerticalBox(HealthText);
	USizeBox* HealthSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HealthSize"));
	HealthSize->SetWidthOverride(340.f);
	HealthSize->SetHeightOverride(22.f);
	HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	HealthBar->SetFillColorAndOpacity(FLinearColor(0.15f, 0.85f, 0.25f));
	HealthBar->SetPercent(1.f);
	HealthSize->SetContent(HealthBar);
	if (UVerticalBoxSlot* BarSlot = HealthColumn->AddChildToVerticalBox(HealthSize))
	{
		BarSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
	if (UCanvasPanelSlot* HealthSlot = Canvas->AddChildToCanvas(HealthColumn))
	{
		HealthSlot->SetAnchors(FAnchors(0.f, 1.f));
		HealthSlot->SetAlignment(FVector2D(0.f, 1.f));
		HealthSlot->SetPosition(FVector2D(40.f, -40.f));
		HealthSlot->SetAutoSize(true);
	}

	// Bottom right, fixed in place: the ability squares (dash, melee) and, to their right, the ammo counter in a slot of
	// its own (so a changing or missing ammo count never moves the squares).
	auto MakeAbilitySquare = [this, &MakeText](const TCHAR* Name, const FText& Label, int32 LabelSize, TObjectPtr<USizeBox>& OutShade)
	{
		USizeBox* Square = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("%sSquare"), Name));
		Square->SetWidthOverride(MeleeIconSize);
		Square->SetHeightOverride(MeleeIconSize);
		UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *FString::Printf(TEXT("%sStack"), Name));
		Square->SetContent(Stack);
		UBorder* Back = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("%sBack"), Name));
		Back->SetBrushColor(FLinearColor(0.08f, 0.08f, 0.1f, 0.75f));
		Back->SetHorizontalAlignment(HAlign_Center);
		Back->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Text = MakeText(*FString::Printf(TEXT("%sLabel"), Name), LabelSize);
		Text->SetText(Label);	// placeholder icon: the key
		Back->SetContent(Text);
		if (UOverlaySlot* BackSlot = Stack->AddChildToOverlay(Back))
		{
			BackSlot->SetHorizontalAlignment(HAlign_Fill);
			BackSlot->SetVerticalAlignment(VAlign_Fill);
		}
		// The cooldown shade sits on the bottom and shrinks, so the icon is revealed from the top down.
		OutShade = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("%sShade"), Name));
		OutShade->SetHeightOverride(0.f);
		UImage* ShadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("%sShadeImage"), Name));
		ShadeImage->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f));
		OutShade->SetContent(ShadeImage);
		if (UOverlaySlot* ShadeSlot = Stack->AddChildToOverlay(OutShade))
		{
			ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
			ShadeSlot->SetVerticalAlignment(VAlign_Bottom);
		}
		return Square;
	};
	UHorizontalBox* Abilities = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Abilities"));
	if (UHorizontalBoxSlot* DashSlot = Abilities->AddChildToHorizontalBox(MakeAbilitySquare(TEXT("Dash"), LOCTEXT("DashKey", "SHIFT"), 13, DashShade)))
	{
		DashSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	}
	Abilities->AddChildToHorizontalBox(MakeAbilitySquare(TEXT("Melee"), LOCTEXT("MeleeKey", "F"), 22, MeleeShade));
	if (UCanvasPanelSlot* AbilitySlot = Canvas->AddChildToCanvas(Abilities))
	{
		AbilitySlot->SetAnchors(FAnchors(1.f, 1.f));
		AbilitySlot->SetAlignment(FVector2D(1.f, 1.f));
		AbilitySlot->SetPosition(FVector2D(-40.f - AmmoSlotWidth - 18.f, -40.f));
		AbilitySlot->SetAutoSize(true);
	}

	USizeBox* AmmoBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AmmoBox"));
	AmmoBox->SetWidthOverride(AmmoSlotWidth);
	AmmoBox->SetHeightOverride(MeleeIconSize);
	AmmoText = MakeText(TEXT("AmmoText"), 34);
	AmmoText->SetJustification(ETextJustify::Right);
	if (USizeBoxSlot* AmmoTextSlot = Cast<USizeBoxSlot>(AmmoBox->SetContent(AmmoText)))
	{
		AmmoTextSlot->SetHorizontalAlignment(HAlign_Right);
		AmmoTextSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UCanvasPanelSlot* AmmoCanvasSlot = Canvas->AddChildToCanvas(AmmoBox))
	{
		AmmoCanvasSlot->SetAnchors(FAnchors(1.f, 1.f));
		AmmoCanvasSlot->SetAlignment(FVector2D(1.f, 1.f));
		AmmoCanvasSlot->SetPosition(FVector2D(-40.f, -40.f));
		AmmoCanvasSlot->SetAutoSize(true);
	}

	// Under the crosshair: reload progress (only while reloading).
	USizeBox* ReloadSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReloadSize"));
	ReloadSize->SetWidthOverride(160.f);
	ReloadSize->SetHeightOverride(8.f);
	ReloadBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ReloadBar"));
	ReloadBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.85f, 0.2f));
	ReloadSize->SetContent(ReloadBar);
	if (UCanvasPanelSlot* ReloadSlot = Canvas->AddChildToCanvas(ReloadSize))
	{
		ReloadSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ReloadSlot->SetAlignment(FVector2D(0.5f, 0.f));
		ReloadSlot->SetPosition(FVector2D(0.f, 44.f));
		ReloadSlot->SetAutoSize(true);
	}
	ReloadSize->SetVisibility(ESlateVisibility::Collapsed);
}

void UFPSRLCombatHUDWidget::SetController(AFPSRLPlayerController* InController)
{
	Controller = InController;
	CheckBindings();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(BindTimer, this, &ThisClass::CheckBindings, 0.25f, true);
	}
}

void UFPSRLCombatHUDWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindTimer);
		World->GetTimerManager().ClearTimer(AnimTimer);
	}
	if (UFPSRLHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
	}
	if (AFPSRLWeapon* Weapon = BoundWeapon.Get())
	{
		Weapon->OnWeaponStateChanged.Remove(WeaponHandle);
	}
	Super::NativeDestruct();
}

void UFPSRLCombatHUDWidget::CheckBindings()
{
	// The pawn and weapon change (spawn, travel, weapon pick), so re-find them now and then; updates are event-driven.
	const APawn* Pawn = Controller.IsValid() ? Controller->GetPawn() : nullptr;
	UFPSRLHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
	if (Health != BoundHealth.Get())
	{
		if (UFPSRLHealthComponent* Old = BoundHealth.Get())
		{
			Old->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
		}
		BoundHealth = Health;
		LastHealth = -1.0;
		if (Health)
		{
			Health->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
			HandleHealthChanged(Health->GetCurrentHealth(), Health->GetMaxHealth());
		}
	}

	AFPSRLWeapon* Weapon = nullptr;
	if (Pawn)
	{
		TArray<AActor*> Attached;
		Pawn->GetAttachedActors(Attached);
		for (AActor* Actor : Attached)
		{
			AFPSRLWeapon* Candidate = Cast<AFPSRLWeapon>(Actor);
			if (Candidate && !Candidate->IsHidden())
			{
				Weapon = Candidate;
				break;
			}
		}
	}
	if (Weapon != BoundWeapon.Get())
	{
		if (AFPSRLWeapon* Old = BoundWeapon.Get())
		{
			Old->OnWeaponStateChanged.Remove(WeaponHandle);
		}
		BoundWeapon = Weapon;
		if (Weapon)
		{
			WeaponHandle = Weapon->OnWeaponStateChanged.AddUObject(this, &ThisClass::RefreshAmmo);
		}
	}
	RefreshAmmo();

	// Melee: a swing may have started since the last check.
	if ((Controller.IsValid() && Controller->GetMeleeCooldownFraction() > 0.f) || GetDashCooldownFraction() > 0.f)
	{
		StartAnimating();
	}
}

void UFPSRLCombatHUDWidget::HandleHealthChanged(double CurrentHealth, double MaxHealth)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(MaxHealth > 0.0 ? static_cast<float>(FMath::Clamp(CurrentHealth / MaxHealth, 0.0, 1.0)) : 0.f);
	}
	if (HealthText)
	{
		HealthText->SetText(FText::Format(LOCTEXT("Health", "Health  {0} / {1}"), FText::AsNumber(FMath::CeilToInt(CurrentHealth)), FText::AsNumber(FMath::CeilToInt(MaxHealth))));
	}
	if (LastHealth >= 0.0 && CurrentHealth < LastHealth - 0.01)
	{
		FlashStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;	// took damage
		StartAnimating();
	}
	LastHealth = CurrentHealth;
}

void UFPSRLCombatHUDWidget::RefreshAmmo()
{
	const AFPSRLWeapon* Weapon = BoundWeapon.Get();
	if (AmmoText)
	{
		AmmoText->SetVisibility(Weapon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (Weapon)
		{
			AmmoText->SetText(FText::Format(LOCTEXT("Ammo", "{0} / {1}"), FText::AsNumber(Weapon->GetCurrentBullets()), FText::AsNumber(Weapon->GetMagSize())));
		}
	}
	if (Weapon && Weapon->IsReloading())
	{
		StartAnimating();
	}
}

void UFPSRLCombatHUDWidget::StartAnimating()
{
	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(AnimTimer))
	{
		World->GetTimerManager().SetTimer(AnimTimer, this, &ThisClass::Animate, 1.f / 30.f, true);
	}
	Animate();
}

void UFPSRLCombatHUDWidget::Animate()
{
	bool bMoving = false;

	// Melee square: the shade covers the remaining share of the cooldown, anchored to the bottom.
	const float Cooldown = Controller.IsValid() ? Controller->GetMeleeCooldownFraction() : 0.f;
	if (MeleeShade)
	{
		MeleeShade->SetHeightOverride(MeleeIconSize * Cooldown);
	}
	bMoving |= Cooldown > 0.f;

	// Dash square, same idea (the character's own cooldown).
	const float DashCooldown = GetDashCooldownFraction();
	if (DashShade)
	{
		DashShade->SetHeightOverride(MeleeIconSize * DashCooldown);
	}
	bMoving |= DashCooldown > 0.f;

	// Reload bar under the crosshair.
	const AFPSRLWeapon* Weapon = BoundWeapon.Get();
	const bool bReloading = Weapon && Weapon->IsReloading();
	if (ReloadBar)
	{
		if (UWidget* ReloadBox = ReloadBar->GetParent())
		{
			ReloadBox->SetVisibility(bReloading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		ReloadBar->SetPercent(bReloading ? Weapon->GetReloadProgress() : 0.f);
	}
	bMoving |= bReloading;

	// Damage flash fades out.
	const double SinceHit = GetWorld() ? GetWorld()->GetTimeSeconds() - FlashStartTime : 1000.0;
	const float Alpha = SinceHit < DamageFlashDuration ? DamageFlashOpacity * static_cast<float>(1.0 - SinceHit / DamageFlashDuration) : 0.f;
	if (DamageFlash)
	{
		DamageFlash->SetColorAndOpacity(FLinearColor(1.f, 0.f, 0.f, Alpha));
	}
	bMoving |= Alpha > 0.f;

	if (!bMoving && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AnimTimer);
	}
}

float UFPSRLCombatHUDWidget::GetDashCooldownFraction()
{
	const APawn* Pawn = Controller.IsValid() ? Controller->GetPawn() : nullptr;
	const FBoolProperty* CanDash = Pawn ? CastField<FBoolProperty>(Pawn->GetClass()->FindPropertyByName(TEXT("bCanDash"))) : nullptr;
	if (!CanDash || CanDash->GetPropertyValue_InContainer(Pawn))
	{
		DashLockStart = -1.0;
		return 0.f;	// ready (or a pawn without a dash)
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (DashLockStart < 0.0)
	{
		DashLockStart = Now;
	}
	double Cooldown = 1.0;
	if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Pawn->GetClass()->FindPropertyByName(TEXT("DashCooldown"))))
	{
		Cooldown = FMath::Max(0.01, AsDouble->GetPropertyValue_InContainer(Pawn));
	}
	return FMath::Clamp(static_cast<float>(1.0 - (Now - DashLockStart) / Cooldown), 0.01f, 1.f);	// never 0 while still locked
}

FString UFPSRLCombatHUDWidget::DescribeForTest() const
{
	auto Text = [](const UTextBlock* Block) { return Block && Block->GetVisibility() != ESlateVisibility::Collapsed ? Block->GetText().ToString() : FString(TEXT("-")); };
	const UWidget* ReloadBox = ReloadBar ? ReloadBar->GetParent() : nullptr;
	return FString::Printf(TEXT("health '%s' bar %.2f | ammo '%s' | dash shade %.0f px | melee shade %.0f px | reload bar %s %.2f | damage flash %.2f"),
		*Text(HealthText), HealthBar ? HealthBar->GetPercent() : -1.f, *Text(AmmoText), DashShade ? DashShade->GetHeightOverride() : -1.f, MeleeShade ? MeleeShade->GetHeightOverride() : -1.f,
		ReloadBox && ReloadBox->GetVisibility() != ESlateVisibility::Collapsed ? TEXT("shown") : TEXT("hidden"), ReloadBar ? ReloadBar->GetPercent() : -1.f,
		DamageFlash ? DamageFlash->GetColorAndOpacity().A : -1.f);
}

#undef LOCTEXT_NAMESPACE
