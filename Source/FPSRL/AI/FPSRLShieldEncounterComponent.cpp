// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLShieldEncounterComponent.h"
#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "Combat/FPSRLGroundStrike.h"
#include "Combat/FPSRLLandmine.h"
#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Data/FPSRLEnemyBehaviorProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Rooms/FPSRLShieldPlatform.h"
#include "TimerManager.h"
#include "FPSRL.h"

namespace FPSRLShieldEncounter
{
	static TAutoConsoleVariable<float> CVarMechanicInterval(TEXT("fpsrl.Juggernaut.MechanicInterval"), 0.f,
		TEXT("Test: seconds between the Juggernaut's platform mechanics instead of its profile's (0 = the profile's)."));
	static const TCHAR* ShieldMaterial = TEXT("/Game/MainProject/Contents/Materials/Enemies/M_EnemyPhased.M_EnemyPhased");
	static const TCHAR* DomeMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static constexpr float CheckInterval = 0.1f;
	static constexpr float PlatformSearchRadius = 6000.f;
	static constexpr float PlayerSearchRadius = 8000.f;
}

UFPSRLShieldEncounterComponent::UFPSRLShieldEncounterComponent()
{
	SetIsReplicatedByDefault(true);
}

void UFPSRLShieldEncounterComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, ShieldsRemaining);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, ShieldLayers);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, Phase);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, DisruptionProgress);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, DisruptionSeconds);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, RequiredCount);
	DOREPLIFETIME(UFPSRLShieldEncounterComponent, bForcedPullShown);
}

void UFPSRLShieldEncounterComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority())
	{
		OnRep_State();
		return;
	}
	// Shielded from the start (its AI and profile come a moment later: Watch sets the layers).
	if (UFPSRLHealthComponent* Health = GetOwner()->FindComponentByClass<UFPSRLHealthComponent>())
	{
		Health->SetInvulnerable(true);
		Health->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleDeath);
	}
	GetWorld()->GetTimerManager().SetTimer(WatchTimer, this, &ThisClass::Watch, 0.5f, true, 0.1f);
}

void UFPSRLShieldEncounterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndMechanic(false);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Super::EndPlay(EndPlayReason);
}

const UFPSRLEnemyBehaviorProfile* UFPSRLShieldEncounterComponent::GetProfile() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AFPSRLEnemyAIController* AI = Pawn ? Cast<AFPSRLEnemyAIController>(Pawn->GetController()) : nullptr;
	return AI ? AI->GetProfile() : nullptr;
}

void UFPSRLShieldEncounterComponent::Watch()
{
	// Waits for its AI (the profile) and its encounter to start, then runs the mechanic on its own timers.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AFPSRLEnemyAIController* AI = Pawn ? Cast<AFPSRLEnemyAIController>(Pawn->GetController()) : nullptr;
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	if (bActive || bDead || !AI || !Profile || AI->GetAIState() == EFPSRLEnemyAIState::Inactive)
	{
		return;
	}
	bActive = true;
	BlindSince = GetWorld()->GetTimeSeconds();	// the blind clock starts now (players are still coming in)
	GetWorld()->GetTimerManager().ClearTimer(WatchTimer);
	ShieldLayers = Profile->ShieldLayers;
	ShieldsRemaining = ShieldLayers;
	DisruptionSeconds = Profile->ShieldDisruptionSeconds;
	if (UFPSRLHealthComponent* Health = GetOwner()->FindComponentByClass<UFPSRLHealthComponent>())
	{
		Health->SetInvulnerable(ShieldsRemaining > 0);
	}
	SetPhase(ShieldsRemaining > 0 ? EFPSRLShieldPhase::Idle : EFPSRLShieldPhase::Vulnerable);
	OnRep_State();	// the HUD and the shield glow (the server has no OnRep)
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] %s: encounter on, %d shield layer(s)"), *GetOwner()->GetName(), ShieldsRemaining);
	ScheduleMechanic();
	GetWorld()->GetTimerManager().SetTimer(BlindTimer, this, &ThisClass::CheckSight, 0.5f, true);
}

void UFPSRLShieldEncounterComponent::ScheduleMechanic()
{
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	if (ShieldsRemaining <= 0 || bDead || !Profile)
	{
		return;
	}
	const float Override = FPSRLShieldEncounter::CVarMechanicInterval.GetValueOnGameThread();
	GetWorld()->GetTimerManager().SetTimer(MechanicTimer, this, &ThisClass::StartMechanic, Override > 0.f ? Override : Profile->ShieldMechanicInterval, false);
}

void UFPSRLShieldEncounterComponent::StartMechanicNow()
{
	if (GetOwner()->HasAuthority() && bActive && Phase == EFPSRLShieldPhase::Idle)
	{
		GetWorld()->GetTimerManager().ClearTimer(MechanicTimer);
		StartMechanic();
	}
}

TArray<APawn*> UFPSRLShieldEncounterComponent::GetStandingPlayers() const
{
	TArray<APawn*> Players;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	for (const APlayerState* PlayerState : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		APawn* Player = PlayerState ? PlayerState->GetPawn() : nullptr;
		const UFPSRLHealthComponent* Health = Player ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		if (Health && !Health->IsDead() && !Health->IsDowned()
			&& FVector::Dist(Player->GetActorLocation(), GetOwner()->GetActorLocation()) < FPSRLShieldEncounter::PlayerSearchRadius)
		{
			Players.Add(Player);
		}
	}
	return Players;
}

void UFPSRLShieldEncounterComponent::StartMechanic()
{
	StartPull(false);
}

void UFPSRLShieldEncounterComponent::StartPull(bool bForced)
{
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	ACharacter* Boss = Cast<ACharacter>(GetOwner());
	const TArray<APawn*> Players = GetStandingPlayers();
	const TArray<AFPSRLShieldPlatform*> Platforms = AFPSRLShieldPlatform::FindAround(GetWorld(), Boss->GetActorLocation(), FPSRLShieldEncounter::PlatformSearchRadius);
	if (!bForced && Phase != EFPSRLShieldPhase::Idle)
	{
		GetWorld()->GetTimerManager().SetTimer(MechanicTimer, this, &ThisClass::StartMechanic, 3.f, false);	// a forced pull is under way
		return;
	}
	if (!Profile || bDead || Players.IsEmpty() || (!bForced && (ShieldsRemaining <= 0 || Platforms.IsEmpty())))
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] pull skipped (%d standing player(s), %d platform(s))"), Players.Num(), Platforms.Num());
		if (!bForced)
		{
			ScheduleMechanic();
		}
		return;
	}
	SetAttacksPaused(true);
	bForcedPull = bForced;
	bForcedPullShown = bForced;

	// 1. The warning: every mine it has thrown out at once, spread all round it. Then the pull.
	SetPhase(EFPSRLShieldPhase::Throwing);
	AFPSRLLandmine::ThrowAround(Boss, Profile->ShieldMines);
	GetWorld()->GetTimerManager().SetTimer(ChargeTimer, this, &ThisClass::PullAndCharge, Profile->ShieldMines.ThrowSeconds + 0.2f, false);
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] %s: mines thrown, the pull comes in %.1f s"), bForced ? TEXT("FORCED PULL (nobody in sight)") : TEXT("platform mechanic"),
		Profile->ShieldMines.ThrowSeconds + 0.2f);
}

void UFPSRLShieldEncounterComponent::PullAndCharge()
{
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	ACharacter* Boss = Cast<ACharacter>(GetOwner());
	TArray<APawn*> Players = GetStandingPlayers();
	TArray<AFPSRLShieldPlatform*> Platforms = AFPSRLShieldPlatform::FindAround(GetWorld(), Boss->GetActorLocation(), FPSRLShieldEncounter::PlatformSearchRadius);
	if (!Profile || bDead || Players.IsEmpty())
	{
		EndMechanic(!bForcedPull);
		return;
	}

	// 1. The standing players: brought next to the boss, spread around it (never inside each other), facing it.
	Required.Reset();
	const FVector Center = Boss->GetActorLocation();
	const float Floor = Center.Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (int32 Index = 0; Index < Players.Num(); ++Index)
	{
		ACharacter* Player = Cast<ACharacter>(Players[Index]);
		Required.Add(Players[Index]);
		if (!Player)
		{
			continue;
		}
		const FVector Direction = Boss->GetActorForwardVector().RotateAngleAxis(360.f * Index / Players.Num(), FVector::UpVector);
		FVector Spot = FVector(Center.X, Center.Y, Floor) + Direction * (Profile->ShieldRepositionRadius + Boss->GetCapsuleComponent()->GetScaledCapsuleRadius());
		FNavLocation OnNav;
		if (Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(200.f, 200.f, 300.f)))
		{
			Spot = OnNav.Location;
		}
		const FRotator Facing = (Center - Spot).GetSafeNormal2D().Rotation();
		Player->TeleportTo(Spot + FVector(0.f, 0.f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Facing, false, true);
		if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
		{
			PC->ClientSetRotation(Facing, true);
		}
	}
	RequiredCount = Required.Num();

	// 2. As many platforms as standing players (up to the four), a different combination than last time when possible.
	//    Not for a forced pull: it only punishes hiding (no shield progress).
	if (bForcedPull)
	{
		Platforms.Reset();
	}
	const int32 Count = FMath::Clamp(Required.Num(), 1, FMath::Max(1, Platforms.Num()));
	uint32 Selection = 0;
	for (int32 Try = 0; Try < 12 && !Platforms.IsEmpty() && (Selection == 0 || (Selection == LastSelection && Count < Platforms.Num())); ++Try)
	{
		TArray<int32> Order;
		for (int32 Index = 0; Index < Platforms.Num(); ++Index)
		{
			Order.Add(Index);
		}
		for (int32 Index = Order.Num() - 1; Index > 0; --Index)
		{
			Order.Swap(Index, FMath::RandRange(0, Index));
		}
		Selection = 0;
		for (int32 Pick = 0; Pick < Count; ++Pick)
		{
			Selection |= 1u << Order[Pick];
		}
	}
	if (!Platforms.IsEmpty())
	{
		LastSelection = Selection;
	}
	Highlighted.Reset();
	TArray<FString> Names;
	for (int32 Index = 0; Index < Platforms.Num(); ++Index)
	{
		const bool bLit = (Selection & (1u << Index)) != 0;
		Platforms[Index]->SetHighlighted(bLit);
		if (bLit)
		{
			Highlighted.Add(Platforms[Index]);
			Names.Add(Platforms[Index]->PlatformName.ToString());
		}
	}

	// 3. The charge: its blast area fills on the floor while it charges (a forced pull barely waits).
	const float Charge = bForcedPull ? Profile->ForcedPullChargeSeconds : Profile->ShieldChargeSeconds;
	DisruptionProgress = 0.f;
	SetPhase(EFPSRLShieldPhase::Charging);
	AFPSRLGroundStrike::Spawn(Boss, FVector(Center.X, Center.Y, Floor), Profile->ShieldShockwaveRadius, Charge, 0.f);
	if (UFPSRLEnemyRoleComponent* RoleView = Boss->FindComponentByClass<UFPSRLEnemyRoleComponent>())
	{
		RoleView->PlayAction(TEXT("ShieldCharge"), Charge);
	}
	GetWorld()->GetTimerManager().SetTimer(ChargeTimer, this, &ThisClass::ReleaseShockwave, Charge, false);
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] %d standing player(s) pulled in, platforms lit [%s], charging %.1f s (%s)"), Required.Num(),
		*FString::Join(Names, TEXT(", ")), Charge, bForcedPull ? TEXT("forced: no platforms") : *FString::Printf(TEXT("shield %d of %d"), ShieldLayers - ShieldsRemaining + 1, ShieldLayers));
}

void UFPSRLShieldEncounterComponent::ReleaseShockwave()
{
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	ACharacter* Boss = Cast<ACharacter>(GetOwner());
	if (!Profile || !Boss || bDead)
	{
		return;
	}
	const FVector Floor = Boss->GetActorLocation() - FVector(0.f, 0.f, Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	AFPSRLShockwave::Spawn(Boss, Floor, Profile->ShieldShockwaveDamage, Profile->ShieldShockwaveRadius, Profile->ShieldShockwaveSpeed, Profile->ShieldShockwaveHeight);
	if (bForcedPull)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] forced pull over (shockwave %.0f); no platforms"), Profile->ShieldShockwaveDamage);
		NextForcedPull = GetWorld()->GetTimeSeconds() + Profile->ForcedPullCooldown;
		EndMechanic(false);
		return;
	}
	SetPhase(EFPSRLShieldPhase::AwaitingPlayers);
	LastCheckTime = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(CheckTimer, this, &ThisClass::CheckPlayers, FPSRLShieldEncounter::CheckInterval, true);
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] shockwave: %.0f damage out to %.0f cm"), Profile->ShieldShockwaveDamage, Profile->ShieldShockwaveRadius);
}

void UFPSRLShieldEncounterComponent::UpdateRequired()
{
	// Downed / dead / gone players stop counting; a lit platform nobody can fill any more goes dark (unoccupied ones first).
	Required.RemoveAll([](const TWeakObjectPtr<APawn>& Player)
	{
		const UFPSRLHealthComponent* Health = Player.IsValid() ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
		return !Health || Health->IsDead() || Health->IsDowned();
	});
	RequiredCount = Required.Num();
	Highlighted.RemoveAll([](const TWeakObjectPtr<AFPSRLShieldPlatform>& Platform) { return !Platform.IsValid(); });
	while (Highlighted.Num() > FMath::Max(1, Required.Num()))
	{
		int32 Drop = Highlighted.Num() - 1;
		for (int32 Index = 0; Index < Highlighted.Num(); ++Index)
		{
			const bool bOccupied = Required.ContainsByPredicate([&](const TWeakObjectPtr<APawn>& Player) { return Highlighted[Index]->IsStandingOn(Player.Get()); });
			if (!bOccupied)
			{
				Drop = Index;
				break;
			}
		}
		Highlighted[Drop]->SetHighlighted(false);
		Highlighted.RemoveAt(Drop);
	}
}

void UFPSRLShieldEncounterComponent::CheckPlayers()
{
	UpdateRequired();
	if (Required.IsEmpty())
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] platform mechanic called off: nobody is standing"));
		EndMechanic(true);
		return;
	}
	// Every standing player on a lit platform, every lit platform taken (one each).
	bool bAllPlaced = true;
	for (const TWeakObjectPtr<APawn>& Player : Required)
	{
		bAllPlaced &= Highlighted.ContainsByPredicate([&](const TWeakObjectPtr<AFPSRLShieldPlatform>& Platform) { return Platform->IsStandingOn(Player.Get()); });
	}
	for (const TWeakObjectPtr<AFPSRLShieldPlatform>& Platform : Highlighted)
	{
		bAllPlaced &= Required.ContainsByPredicate([&](const TWeakObjectPtr<APawn>& Player) { return Platform->IsStandingOn(Player.Get()); });
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float Delta = static_cast<float>(Now - LastCheckTime);
	LastCheckTime = Now;
	if (!bAllPlaced)
	{
		if (Phase == EFPSRLShieldPhase::Disrupting)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] Shield Disruption paused at %.1f s"), DisruptionProgress);
		}
		SetPhase(EFPSRLShieldPhase::AwaitingPlayers);	// paused: the progress stays
		return;
	}
	if (Phase != EFPSRLShieldPhase::Disrupting)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] every standing player is on a lit platform: Shield Disruption %s at %.1f s"),
			DisruptionProgress > 0.f ? TEXT("resumes") : TEXT("starts"), DisruptionProgress);
	}
	SetPhase(EFPSRLShieldPhase::Disrupting);
	DisruptionProgress = FMath::Min(DisruptionSeconds, DisruptionProgress + Delta);
	OnRep_State();
	if (DisruptionProgress >= DisruptionSeconds)
	{
		BreakShield();
	}
}

void UFPSRLShieldEncounterComponent::BreakShield()
{
	ShieldsRemaining = FMath::Max(0, ShieldsRemaining - 1);
	OnRep_State();
	GetOwner()->ForceNetUpdate();
	ACharacter* Boss = Cast<ACharacter>(GetOwner());
	// The break: a burst around the boss (bigger for the last layer); visual only.
	const FVector Floor = Boss->GetActorLocation() - FVector(0.f, 0.f, Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	AFPSRLGroundStrike::Spawn(Boss, Floor, ShieldsRemaining > 0 ? 500.f : 900.f, 0.f, -1.f);
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] SHIELD %d BROKEN (%d left)%s"), ShieldLayers - ShieldsRemaining, ShieldsRemaining,
		ShieldsRemaining > 0 ? TEXT("") : TEXT(": fully vulnerable"));
	if (ShieldsRemaining <= 0)
	{
		if (UFPSRLHealthComponent* Health = GetOwner()->FindComponentByClass<UFPSRLHealthComponent>())
		{
			Health->SetInvulnerable(false);
		}
	}
	EndMechanic(ShieldsRemaining > 0);
	if (ShieldsRemaining <= 0)
	{
		SetPhase(EFPSRLShieldPhase::Vulnerable);
	}
}

void UFPSRLShieldEncounterComponent::EndMechanic(bool bResumeLater)
{
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	Timers.ClearTimer(ChargeTimer);
	Timers.ClearTimer(CheckTimer);
	for (const TWeakObjectPtr<AFPSRLShieldPlatform>& Platform : Highlighted)
	{
		if (Platform.IsValid())
		{
			Platform->SetHighlighted(false);
		}
	}
	Highlighted.Reset();
	Required.Reset();
	RequiredCount = 0;
	DisruptionProgress = 0.f;
	bForcedPullShown = false;
	bForcedPull = false;
	SetPhase(ShieldsRemaining > 0 ? EFPSRLShieldPhase::Idle : EFPSRLShieldPhase::Vulnerable);
	SetAttacksPaused(false);
	if (bResumeLater)
	{
		ScheduleMechanic();
	}
}

void UFPSRLShieldEncounterComponent::SetPhase(EFPSRLShieldPhase NewPhase)
{
	if (Phase != NewPhase)
	{
		Phase = NewPhase;
		OnRep_State();
		GetOwner()->ForceNetUpdate();
	}
}

void UFPSRLShieldEncounterComponent::SetAttacksPaused(bool bPaused)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AFPSRLEnemyAIController* AI = Pawn ? Cast<AFPSRLEnemyAIController>(Pawn->GetController()) : nullptr)
	{
		AI->SetAttacksPaused(bPaused);
	}
}

void UFPSRLShieldEncounterComponent::HandleDeath(AController* Killer, AActor* Causer)
{
	bDead = true;
	GetWorld()->GetTimerManager().ClearTimer(BlindTimer);
	GetWorld()->GetTimerManager().ClearTimer(MechanicTimer);
	EndMechanic(false);
}

TArray<AFPSRLShieldPlatform*> UFPSRLShieldEncounterComponent::GetHighlightedPlatforms() const
{
	TArray<AFPSRLShieldPlatform*> Lit;
	for (const TWeakObjectPtr<AFPSRLShieldPlatform>& Platform : Highlighted)
	{
		if (Platform.IsValid())
		{
			Lit.Add(Platform.Get());
		}
	}
	return Lit;
}

void UFPSRLShieldEncounterComponent::OnRep_State()
{
	// The shield: a see-through cyan dome over the boss while any layer stands.
	if (LastShieldsShown != ShieldsRemaining && GetNetMode() != NM_DedicatedServer)
	{
		LastShieldsShown = ShieldsRemaining;
		ACharacter* Boss = Cast<ACharacter>(GetOwner());
		if (!Dome && Boss && ShieldsRemaining > 0)
		{
			Dome = NewObject<UStaticMeshComponent>(Boss, TEXT("ShieldDome"));
			Dome->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, FPSRLShieldEncounter::DomeMesh));
			Dome->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, FPSRLShieldEncounter::ShieldMaterial));
			Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Dome->SetCastShadow(false);
			Dome->SetUsingAbsoluteScale(true);
			Dome->SetupAttachment(Boss->GetRootComponent());
			Dome->RegisterComponent();
			// Around the whole body (the sphere is 100 cm across).
			const float Size = Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.6f / 100.f;
			Dome->SetWorldScale3D(FVector(Size, Size, Size));
		}
		if (Dome)
		{
			Dome->SetVisibility(ShieldsRemaining > 0);
		}
		UE_LOG(LogFPSRL, Verbose, TEXT("[Juggernaut] shield dome %s (%d layer(s))"), ShieldsRemaining > 0 ? TEXT("up") : TEXT("gone"), ShieldsRemaining);
	}
	OnStateChanged.Broadcast();
}

void UFPSRLShieldEncounterComponent::CheckSight()
{
	// Nobody it can see for ForcedPullBlindSeconds (players hiding behind cover / out of its sight): a forced pull.
	const UFPSRLEnemyBehaviorProfile* Profile = GetProfile();
	const APawn* Boss = Cast<APawn>(GetOwner());
	const AFPSRLEnemyAIController* AI = Boss ? Cast<AFPSRLEnemyAIController>(Boss->GetController()) : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Profile || !AI || bDead || (Phase != EFPSRLShieldPhase::Idle && Phase != EFPSRLShieldPhase::Vulnerable))
	{
		BlindSince = Now;
		return;
	}
	const TArray<APawn*> Players = GetStandingPlayers();
	const bool bSeesAnyone = Players.ContainsByPredicate([AI](const APawn* Player) { return AI->HasLineOfSightTo(Player); });
	if (bSeesAnyone || Players.IsEmpty())
	{
		BlindSince = Now;
		return;
	}
	if (Now - BlindSince >= Profile->ForcedPullBlindSeconds && Now >= NextForcedPull)
	{
		UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] nobody in sight for %.1f s"), Now - BlindSince);
		BlindSince = Now;
		StartPull(true);
	}
}
