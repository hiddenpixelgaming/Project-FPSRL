// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "FPSRLTeamHUDWidget.generated.h"

class AFPSRLPlayerState;
class UAbilitySystemComponent;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
struct FOnAttributeChangeData;

/**
 * One teammate on the teammate HUD: name, health bar and speaking state (the name turns yellow while they talk).
 *
 * Presentation only. Health comes from the teammate's GAS HealthSet on their PlayerState (replicated to every machine,
 * server-authoritative) through the ASC's attribute-change delegates, so only this entry updates and nothing polls.
 * The speaking state is set from outside (the voice system through UFPSRLTeamHUDWidget::SetSpeakingState); the entry
 * never looks at audio itself. Builds a default layout in code; a Blueprint subclass may supply its own with widgets
 * named PlayerName, HealthBar and StatusText.
 */
UCLASS()
class FPSRL_API UFPSRLTeammateEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Follow this player's state (null = detach and keep showing the last values, e.g. during a Depth travel). */
	void BindPlayer(AFPSRLPlayerState* InPlayer);
	AFPSRLPlayerState* GetPlayer() const { return Player.Get(); }

	void SetSpeaking(bool bInSpeaking);
	bool IsSpeaking() const { return bSpeaking; }

	/** This machine has muted the teammate's voice (shown next to the name). */
	void SetMuted(bool bInMuted);
	bool IsMuted() const { return bMuted; }

	void RefreshName();

	/** Test: "Name 80/100 [speaking] [DOWN]". */
	FString DescribeForTest() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void BeginDestroy() override;

	UPROPERTY(BlueprintReadOnly, Category = "Team", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlayerName;

	UPROPERTY(BlueprintReadOnly, Category = "Team", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	/** "DOWN" / "DEAD" (the game's own downed and dead states); empty otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Team", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(EditDefaultsOnly, Category = "Team")
	FLinearColor NameColor = FLinearColor::White;

	/** The speaking indicator: the name in yellow while the teammate talks. */
	UPROPERTY(EditDefaultsOnly, Category = "Team")
	FLinearColor SpeakingNameColor = FLinearColor(1.f, 0.85f, 0.f);

	UPROPERTY(EditDefaultsOnly, Category = "Team")
	FLinearColor HealthColor = FLinearColor(0.15f, 0.85f, 0.25f);

	UPROPERTY(EditDefaultsOnly, Category = "Team")
	FLinearColor DownedHealthColor = FLinearColor(1.f, 0.45f, 0.1f);

private:
	void Unbind();
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& Data);
	void HandleStatusTagChanged(const FGameplayTag Tag, int32 Count);
	void RefreshHealth();
	void RefreshNameStyle();
	void RefreshStatus();

	TWeakObjectPtr<AFPSRLPlayerState> Player;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthHandle;
	FDelegateHandle MaxHealthHandle;
	FDelegateHandle DownedHandle;
	FDelegateHandle DeadHandle;

	float Health = 0.f;
	float MaxHealth = 0.f;
	bool bDowned = false;
	bool bDead = false;
	bool bSpeaking = false;
	bool bMuted = false;
};

/**
 * The teammate HUD: the OTHER players in the session (never the local player, whose health is the personal HUD's).
 * 0-3 entries in a column at the top left, one per teammate, in a stable order (PlayerId = join order, kept across
 * Depth travel), hidden when playing solo.
 *
 * Session-level: the player controller keeps one instance for the whole session and puts it back on screen after a
 * travel. Entries are keyed by PlayerId (server-assigned, copied to each Depth's new PlayerState), so a travel only
 * re-points an entry at the teammate's new PlayerState; it is removed when the teammate leaves the session
 * (their PlayerState is destroyed, or they do not come back within PruneDelay after a travel).
 *
 * Event-driven only: AFPSRLPlayerState's roster events (begin / end / name changed), the GAS attribute and tag
 * delegates in each entry, and SetSpeakingState from the voice system.
 */
UCLASS()
class FPSRL_API UFPSRLTeamHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The voice system's way in: this teammate is / stops speaking. Unknown ids (the local player, someone who left)
	 *  are ignored. Several teammates may speak at once. */
	void SetSpeakingState(int32 PlayerId, bool bSpeaking);

	/** This machine muted / unmuted a teammate (local only). */
	void SetMutedState(int32 PlayerId, bool bMuted);

	/** Re-checks every PlayerState in the owning player's world (after a travel, or when the local PlayerState arrived). */
	void Resync();

	int32 GetTeammateCount() const { return Entries.Num(); }

	/** Test: "2 teammate(s): Bob 100/100 | Cara 80/100 [speaking]". */
	FString DescribeForTest() const;

	/** Entry widget class (a Blueprint subclass can restyle it). */
	UPROPERTY(EditDefaultsOnly, Category = "Team")
	TSubclassOf<UFPSRLTeammateEntryWidget> EntryClass;

	/** After a travel, entries whose player has not reappeared after this long are removed (they left meanwhile). */
	UPROPERTY(EditDefaultsOnly, Category = "Team")
	float PruneDelay = 30.f;

	/** A teammate's PlayerState destroyed within this long after a Depth travel started is the travel replacing it
	 *  (their new one re-binds the entry); destroyed at any other time, they left the session. */
	UPROPERTY(EditDefaultsOnly, Category = "Team")
	float TravelGrace = 60.f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;

	UPROPERTY(BlueprintReadOnly, Category = "Team", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> EntryList;

private:
	void HandleSeamlessTravelStart(UWorld* World, const FString& LevelName);
	/** The owning controller is gone or going (a travel can replace it): its HUD ignores the roster. */
	bool IsOwnerGone() const;
	void HandlePlayerStateBegin(AFPSRLPlayerState* PlayerState);
	void HandlePlayerStateEnd(AFPSRLPlayerState* PlayerState, EEndPlayReason::Type Reason);
	void HandlePlayerStateChanged(AFPSRLPlayerState* PlayerState);

	/** In the owning player's world, not the local player, not an inactive (disconnected) copy. */
	bool IsTeammate(const AFPSRLPlayerState* PlayerState) const;
	void AddOrRebind(AFPSRLPlayerState* PlayerState);
	void RemoveEntry(int32 PlayerId, const TCHAR* Why);
	void RebuildOrder();
	void SchedulePrune(UWorld* World);
	void PruneDetached();
	void LogState(const TCHAR* Why) const;

	/** PlayerId -> entry. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UFPSRLTeammateEntryWidget>> Entries;

	FDelegateHandle BeginHandle;
	FDelegateHandle EndHandle;
	FDelegateHandle ChangedHandle;
	FDelegateHandle TravelHandle;
	/** When the last Depth travel started on this machine (shared: a travel can replace the controller and its HUD). */
	static double LastTravelStart;
	FTimerHandle PruneTimer;
	TWeakObjectPtr<UWorld> PruneWorld;
};

/**
 * Keeps the teammate HUD for the whole session: a travel replaces the player controller, so the widget belongs to the
 * local player instead and each new controller adopts it (entries, health and speaking state carry over). Cleared
 * when the player is back in the main menu.
 */
UCLASS()
class FPSRL_API UFPSRLTeamHUDSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** The session's teammate HUD, now owned by Owner (made on first use with Class). */
	UFPSRLTeamHUDWidget* GetOrCreateTeamHUD(APlayerController* Owner, TSubclassOf<UFPSRLTeamHUDWidget> Class);

	/** The session is over (main menu): drop the HUD and its entries. */
	void ResetTeamHUD();

private:
	UPROPERTY(Transient)
	TObjectPtr<UFPSRLTeamHUDWidget> TeamHUD;
};
