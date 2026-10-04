// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "Combat/FPSRLShockwave.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/FPSRLCombatRules.h"
#include "Combat/FPSRLWeapon.h"
#include "Components/FPSRLHealthComponent.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Math/RandomStream.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

// --- Pure helpers ------------------------------------------------------------------------------------------------------

namespace FPSRLEnemyAI
{
	int32 SelectTarget(EFPSRLTargetSelection Strategy, const TArray<FTargetCandidate>& Candidates, float PreferredDistance, FRandomStream* Random)
	{
		if (Candidates.IsEmpty())
		{
			return INDEX_NONE;
		}
		auto Best = [&Candidates](TFunctionRef<float(const FTargetCandidate&)> Score)	// lowest score wins; ties: first
		{
			int32 BestIndex = 0;
			for (int32 Index = 1; Index < Candidates.Num(); ++Index)
			{
				if (Score(Candidates[Index]) < Score(Candidates[BestIndex]))
				{
					BestIndex = Index;
				}
			}
			return BestIndex;
		};
		const int32 Closest = Best([](const FTargetCandidate& C) { return C.Distance; });
		switch (Strategy)
		{
		case EFPSRLTargetSelection::LastDamagingPlayer:
		{
			const int32 Damager = Candidates.IndexOfByPredicate([](const FTargetCandidate& C) { return C.bLastDamager; });
			return Damager != INDEX_NONE ? Damager : Closest;
		}
		case EFPSRLTargetSelection::HighestThreat:
		{
			const int32 Top = Best([](const FTargetCandidate& C) { return -C.Threat; });
			return Candidates[Top].Threat > 0.f ? Top : Closest;
		}
		case EFPSRLTargetSelection::LowestHealthPlayer:
			return Best([](const FTargetCandidate& C) { return C.HealthFraction; });
		case EFPSRLTargetSelection::RandomValidPlayer:
			return Random ? Random->RandRange(0, Candidates.Num() - 1) : FMath::RandRange(0, Candidates.Num() - 1);
		case EFPSRLTargetSelection::PreferredRangePlayer:
			return Best([PreferredDistance](const FTargetCandidate& C) { return FMath::Abs(C.Distance - PreferredDistance); });
		case EFPSRLTargetSelection::CurrentTargetUntilInvalid:
		{
			const int32 Current = Candidates.IndexOfByPredicate([](const FTargetCandidate& C) { return C.bCurrent; });
			return Current != INDEX_NONE ? Current : Closest;
		}
		default:
			return Closest;
		}
	}

	const TCHAR* StateName(EFPSRLEnemyAIState State)
	{
		switch (State)
		{
		case EFPSRLEnemyAIState::Inactive:		return TEXT("Inactive");
		case EFPSRLEnemyAIState::Idle:			return TEXT("Idle");
		case EFPSRLEnemyAIState::Detecting:		return TEXT("Detecting");
		case EFPSRLEnemyAIState::Targeting:		return TEXT("Targeting");
		case EFPSRLEnemyAIState::Pursuing:		return TEXT("Pursuing");
		case EFPSRLEnemyAIState::Positioning:	return TEXT("Positioning");
		case EFPSRLEnemyAIState::Attacking:		return TEXT("Attacking");
		case EFPSRLEnemyAIState::Recovering:	return TEXT("Recovering");
		case EFPSRLEnemyAIState::Repositioning:	return TEXT("Repositioning");
		case EFPSRLEnemyAIState::Searching:		return TEXT("Searching");
		case EFPSRLEnemyAIState::Disabled:		return TEXT("Disabled");
		default:								return TEXT("Dead");
		}
	}
}

// --- Setup -------------------------------------------------------------------------------------------------------------

AFPSRLEnemyAIController::AFPSRLEnemyAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))	// crowd avoidance
{
	bStartAILogicOnPossess = false;
	bWantsPlayerState = false;
}

UFPSRLEnemyBehaviorProfile* AFPSRLEnemyAIController::ResolveProfile(const APawn* InPawn) const
{
	if (const UFPSRLEnemyDefinition* Definition = InPawn ? UFPSRLEnemyScalingSettings::Get().FindDefinition(InPawn->GetClass()) : nullptr)
	{
		if (Definition->BehaviorProfile)
		{
			return Definition->BehaviorProfile;
		}
	}
	if (DefaultProfile)
	{
		return DefaultProfile;
	}
	// Nothing configured: a plain shooter (holds its weapon's trigger at range).
	UFPSRLEnemyBehaviorProfile* Fallback = NewObject<UFPSRLEnemyBehaviorProfile>(const_cast<AFPSRLEnemyAIController*>(this), TEXT("FallbackProfile"));
	Fallback->Attacks.AddDefaulted();
	return Fallback;
}

void AFPSRLEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!InPawn || !HasAuthority())
	{
		return;
	}
	SetProfile(ResolveProfile(InPawn));
	HomeLocation = InPawn->GetActorLocation();

	// Face where it aims (the controller's focus), not where it walks.
	if (ACharacter* EnemyCharacter = Cast<ACharacter>(InPawn))
	{
		EnemyCharacter->bUseControllerRotationYaw = true;
		if (UCharacterMovementComponent* Movement = EnemyCharacter->GetCharacterMovement())
		{
			Movement->bOrientRotationToMovement = false;
			Movement->bUseRVOAvoidance = false;	// the crowd following does the avoidance
		}
	}

	Health = InPawn->FindComponentByClass<UFPSRLHealthComponent>();
	if (UFPSRLHealthComponent* HealthComponent = Health.Get())
	{
		DamagedHandle = HealthComponent->OnDamagedBy.AddUObject(this, &ThisClass::HandleDamaged);
		ProjectileHitHandle = HealthComponent->OnProjectileHit.AddUObject(this, &ThisClass::HandleProjectileHit);
		HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	}
	AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InPawn);
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		StunHandle = ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Stunned).AddUObject(this, &ThisClass::HandleStunTagChanged);
	}
	SetState(EFPSRLEnemyAIState::Inactive);	// until its encounter starts
}

void AFPSRLEnemyAIController::OnUnPossess()
{
	if (UFPSRLHealthComponent* HealthComponent = Health.Get())
	{
		HealthComponent->OnDamagedBy.Remove(DamagedHandle);
		HealthComponent->OnProjectileHit.Remove(ProjectileHitHandle);
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::HandleDeath);
	}
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Stunned).Remove(StunHandle);
	}
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	GetWorldTimerManager().ClearTimer(LeapSafetyTimer);
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	GetWorldTimerManager().ClearTimer(AttackTimer);
	Super::OnUnPossess();
}

void AFPSRLEnemyAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	GetWorldTimerManager().ClearTimer(AttackTimer);
	Super::EndPlay(EndPlayReason);
}

void AFPSRLEnemyAIController::SetProfile(UFPSRLEnemyBehaviorProfile* NewProfile)
{
	Profile = NewProfile;
	AttackReadyTimes.Init(0.0, Profile ? Profile->Attacks.Num() : 0);
	if (bEncounterActive && Profile)
	{
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &ThisClass::Think, Profile->ThinkInterval, true, FMath::FRandRange(0.f, Profile->ThinkInterval));
	}
}

void AFPSRLEnemyAIController::SetEncounterActive(bool bActive)
{
	if (!HasAuthority() || !GetPawn() || State == EFPSRLEnemyAIState::Dead || bEncounterActive == bActive)
	{
		return;
	}
	bEncounterActive = bActive;
	if (bActive)
	{
		HomeLocation = GetPawn()->GetActorLocation();
		LastThinkTime = GetWorld()->GetTimeSeconds();
		SetState(EFPSRLEnemyAIState::Idle);
		// Spread the enemies' decisions over the interval so they don't all think on the same frame.
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &ThisClass::Think, Profile->ThinkInterval, true, FMath::FRandRange(0.01f, Profile->ThinkInterval));
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s activated"), *GetPawn()->GetName());
		if (Profile->bAlertedOnEncounterStart)
		{
			AcquireTarget(ChooseTarget(false), true);	// the fight has started: it knows where the players are
		}
	}
	else
	{
		CancelAttack(0.f);
		GetWorldTimerManager().ClearTimer(ThinkTimer);
		GetWorldTimerManager().ClearTimer(AttackTimer);
		StopMovement();
		ClearTarget();
		SetState(EFPSRLEnemyAIState::Inactive);
	}
}

void AFPSRLEnemyAIController::SetState(EFPSRLEnemyAIState NewState)
{
	if (State != NewState)
	{
		UE_LOG(LogFPSRL, VeryVerbose, TEXT("[AI] %s: %s -> %s"), *GetNameSafe(GetPawn()), FPSRLEnemyAI::StateName(State), FPSRLEnemyAI::StateName(NewState));
		State = NewState;
		if (NewState == EFPSRLEnemyAIState::Idle && GetWorld())
		{
			IdleSince = GetWorld()->GetTimeSeconds();
		}
	}
}

FString AFPSRLEnemyAIController::Describe() const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Current = Target.Get();
	FString Players;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	for (const APlayerState* Player : GameState && MyPawn && Profile ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		const APawn* PlayerPawn = Player ? Player->GetPawn() : nullptr;
		if (PlayerPawn)
		{
			const FVector To = (PlayerPawn->GetActorLocation() - MyPawn->GetActorLocation()).GetSafeNormal2D();
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(MyPawn->GetActorForwardVector().GetSafeNormal2D(), To)));
			FHitResult Block;
			FCollisionQueryParams BlockParams(SCENE_QUERY_STAT(FPSRLEnemySightDebug), true, MyPawn);
			BlockParams.AddIgnoredActor(PlayerPawn);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Block, GetEyeLocation(), PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, BlockParams);
			const FString Blocker = bBlocked ? FString::Printf(TEXT(" first hit %s.%s at %.0f cm"), *GetNameSafe(Block.GetActor()), *GetNameSafe(Block.GetComponent()), Block.Distance) : FString();
			Players += FString::Printf(TEXT(" [%s %.0f cm, %s, sight %s, %.0f deg off]"), *Player->GetPlayerName(),
				FVector::Dist(PlayerPawn->GetActorLocation(), MyPawn->GetActorLocation()), IsValidTarget(PlayerPawn) ? TEXT("valid") : TEXT("invalid"),
				HasLineOfSightTo(PlayerPawn) ? TEXT("yes") : TEXT("no"), Angle);
			Players += Blocker;
		}
	}
	const UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	Players += Crowd && Crowd->IsCrowdSimulationEnabled() ? TEXT(" [crowd avoidance on]") : TEXT(" [crowd avoidance OFF]");
	return FString::Printf(TEXT("%s: %s, moved %.0f cm, target %s at %.0f cm%s, attacks %d;%s"), *GetNameSafe(MyPawn), FPSRLEnemyAI::StateName(State),
		MyPawn ? FVector::Dist2D(MyPawn->GetActorLocation(), HomeLocation) : 0.f,
		Current ? *Current->GetName() : TEXT("none"), Current && MyPawn ? FVector::Dist(Current->GetActorLocation(), MyPawn->GetActorLocation()) : 0.f,
		Current && HasLineOfSightTo(Current) ? TEXT(" (seen)") : TEXT(""), AttacksExecuted, *Players);
}

// --- Senses ------------------------------------------------------------------------------------------------------------

FVector AFPSRLEnemyAIController::GetEyeLocation() const
{
	FVector Location;
	FRotator Rotation;
	if (const APawn* MyPawn = GetPawn())
	{
		MyPawn->GetActorEyesViewPoint(Location, Rotation);
	}
	return Location;
}

bool AFPSRLEnemyAIController::HasLineOfSightTo(const AActor* Other) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Other || !GetWorld())
	{
		return false;
	}
	return IsLineClear(GetEyeLocation(), Other->GetActorLocation() + FVector(0.f, 0.f, 40.f), Other);
}

bool AFPSRLEnemyAIController::IsLineClear(const FVector& From, const FVector& To, const AActor* Other) const
{
	// Visibility channel: walls and level geometry block; trigger volumes don't. Bodies (other enemies, players) and what
	// they carry (weapons) don't count as blocking sight.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSRLEnemyLineOfSight), true, GetPawn());
	Params.AddIgnoredActor(Other);
	TArray<AActor*> Carried;
	if (GetPawn())
	{
		GetPawn()->GetAttachedActors(Carried, false, true);
	}
	if (Other)
	{
		TArray<AActor*> OtherCarried;
		Other->GetAttachedActors(OtherCarried, false, true);
		Carried.Append(OtherCarried);
	}
	Params.AddIgnoredActors(Carried);
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
		{
			return true;
		}
		AActor* Blocker = Hit.GetActor();
		const bool bBody = Blocker && (Blocker->IsA<APawn>() || (Blocker->GetOwner() && Blocker->GetOwner()->IsA<APawn>()));
		if (!bBody)
		{
			return false;	// a wall
		}
		Params.AddIgnoredActor(Blocker);
	}
	return true;
}

bool AFPSRLEnemyAIController::IsValidTarget(const AActor* Candidate) const
{
	const APawn* PlayerPawn = Cast<APawn>(Candidate);
	const APawn* MyPawn = GetPawn();
	if (!PlayerPawn || !MyPawn || !IsValid(PlayerPawn) || PlayerPawn->IsPendingKillPending() || !PlayerPawn->GetPlayerState()
		|| !UFPSRLHealthComponent::IsPlayerSide(nullptr, PlayerPawn))
	{
		return false;	// gone, disconnected or not a player
	}
	const UFPSRLHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UFPSRLHealthComponent>();
	if (!PlayerHealth || PlayerHealth->IsDead() || (!Profile->bTargetDownedPlayers && !UFPSRLHealthComponent::IsPawnUp(PlayerPawn)))
	{
		return false;	// dead, or downed when this archetype ignores downed players
	}
	return FVector::Dist(PlayerPawn->GetActorLocation(), MyPawn->GetActorLocation()) <= Profile->MaxTargetDistance;
}

bool AFPSRLEnemyAIController::CanDetect(const AActor* Candidate) const
{
	const APawn* MyPawn = GetPawn();
	const FVector ToCandidate = Candidate->GetActorLocation() - MyPawn->GetActorLocation();
	const float Distance = ToCandidate.Size();
	if (Profile->ProximityAggroRadius > 0.f && Distance <= Profile->ProximityAggroRadius)
	{
		return true;
	}
	if (Distance > Profile->DetectionRange)
	{
		return false;
	}
	if (Profile->DetectionAngle < 360.f)
	{
		const float Cos = FVector::DotProduct(MyPawn->GetActorForwardVector().GetSafeNormal2D(), ToCandidate.GetSafeNormal2D());
		if (Cos < FMath::Cos(FMath::DegreesToRadians(Profile->DetectionAngle * 0.5f)))
		{
			return false;
		}
	}
	return !Profile->bRequireLineOfSightToDetect || HasLineOfSightTo(Candidate);
}

TArray<FPSRLEnemyAI::FTargetCandidate> AFPSRLEnemyAIController::GatherCandidates(bool bDetectableOnly) const
{
	TArray<FPSRLEnemyAI::FTargetCandidate> Candidates;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const APawn* MyPawn = GetPawn();
	if (!GameState || !MyPawn)
	{
		return Candidates;
	}
	// Every active player, not just the host.
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		APawn* PlayerPawn = Player ? Player->GetPawn() : nullptr;
		const bool bCurrent = PlayerPawn && PlayerPawn == Target.Get();
		const bool bDamager = PlayerPawn && PlayerPawn == LastDamager.Get();
		if (!IsValidTarget(PlayerPawn) || (bDetectableOnly && !bCurrent && !bDamager && !CanDetect(PlayerPawn)))
		{
			continue;
		}
		const UFPSRLHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UFPSRLHealthComponent>();
		FPSRLEnemyAI::FTargetCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Actor = PlayerPawn;
		Candidate.Distance = FVector::Dist(PlayerPawn->GetActorLocation(), MyPawn->GetActorLocation());
		Candidate.HealthFraction = PlayerHealth && PlayerHealth->GetMaxHealth() > 0.f ? PlayerHealth->GetCurrentHealth() / PlayerHealth->GetMaxHealth() : 1.f;
		const float* PlayerThreat = Threat.Find(PlayerPawn);
		Candidate.Threat = PlayerThreat ? *PlayerThreat : 0.f;
		Candidate.bLastDamager = bDamager;
		Candidate.bCurrent = bCurrent;
	}
	return Candidates;
}

AActor* AFPSRLEnemyAIController::ChooseTarget(bool bDetectableOnly) const
{
	const TArray<FPSRLEnemyAI::FTargetCandidate> Candidates = GatherCandidates(bDetectableOnly);
	const int32 Pick = FPSRLEnemyAI::SelectTarget(Profile->TargetSelection, Candidates, (Profile->PreferredMinDistance + Profile->PreferredMaxDistance) * 0.5f);
	return Pick != INDEX_NONE ? Candidates[Pick].Actor : nullptr;
}

void AFPSRLEnemyAIController::AcquireTarget(AActor* NewTarget, bool bReact)
{
	if (!NewTarget || NewTarget == Target.Get())
	{
		return;
	}
	const bool bFirst = !Target.IsValid();
	Target = NewTarget;
	SetFocus(NewTarget);
	LastSeenTime = GetWorld()->GetTimeSeconds();
	LastKnownTargetLocation = NewTarget->GetActorLocation();
	NextRetargetTime = LastSeenTime + Profile->RetargetInterval;
	if (bFirst && bReact && Profile->ReactionTime > 0.f)
	{
		ReactionEndTime = LastSeenTime + Profile->ReactionTime;
		SetState(EFPSRLEnemyAIState::Detecting);
	}
	else if (State != EFPSRLEnemyAIState::Attacking && State != EFPSRLEnemyAIState::Recovering)
	{
		SetState(EFPSRLEnemyAIState::Targeting);
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s targets %s"), *GetNameSafe(GetPawn()), *NewTarget->GetName());
}

void AFPSRLEnemyAIController::ClearTarget()
{
	Target.Reset();
	ClearFocus(EAIFocusPriority::Gameplay);
}

// --- Decisions ---------------------------------------------------------------------------------------------------------

void AFPSRLEnemyAIController::Think()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !Profile || State == EFPSRLEnemyAIState::Inactive || State == EFPSRLEnemyAIState::Dead || State == EFPSRLEnemyAIState::Disabled)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float DeltaSeconds = static_cast<float>(Now - LastThinkTime);
	LastThinkTime = Now;
	for (auto It = Threat.CreateIterator(); It; ++It)
	{
		It.Value() -= Profile->ThreatDecayPerSecond * DeltaSeconds;
		if (It.Value() <= 0.f || !It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	// Target validation: a target that left, died, went down or got too far is dropped and another one is looked for.
	if (Target.IsValid() && !IsValidTarget(Target.Get()))
	{
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s drops %s (no longer valid)"), *MyPawn->GetName(), *GetNameSafe(Target.Get()));
		CancelAttack(0.f);
		ClearTarget();
	}

	if (!Target.IsValid())
	{
		if (AActor* Found = ChooseTarget(true))
		{
			AcquireTarget(Found, true);
			return;
		}
		if (State == EFPSRLEnemyAIState::Searching && Now < SearchEndTime)
		{
			return;	// still looking around (moves continue from OnMoveCompleted)
		}
		// Alerted enemies know the fight is on: after a short pause without a target (lost behind a wall, its memory
		// ran out) they take the nearest player again and work their way back into sight, instead of idling.
		if (Profile->bAlertedOnEncounterStart && State == EFPSRLEnemyAIState::Idle && Now - IdleSince >= Profile->IdleRehuntSeconds)
		{
			if (AActor* Anyone = ChooseTarget(false))
			{
				AcquireTarget(Anyone, false);
				return;
			}
		}
		if (FollowSquad())
		{
			return;	// a squad member without a target stays with its leader
		}
		if (State != EFPSRLEnemyAIState::Idle)
		{
			SetState(EFPSRLEnemyAIState::Idle);
			if (Profile->MovementStyle == EFPSRLMovementStyle::GuardPosition)
			{
				MoveToSpot(HomeLocation, EFPSRLEnemyAIState::Idle);
			}
			else
			{
				StopMovement();
			}
		}
		return;
	}

	if (State == EFPSRLEnemyAIState::Detecting)
	{
		if (Now < ReactionEndTime)
		{
			return;
		}
		SetState(EFPSRLEnemyAIState::Targeting);
	}
	if (State == EFPSRLEnemyAIState::Attacking || State == EFPSRLEnemyAIState::Recovering)
	{
		return;	// the attack's timers drive these
	}

	// Re-evaluate the strategy now and then (each enemy on its own).
	if (Profile->TargetSelection != EFPSRLTargetSelection::CurrentTargetUntilInvalid && Now >= NextRetargetTime)
	{
		NextRetargetTime = Now + Profile->RetargetInterval;
		if (AActor* Better = ChooseTarget(true); Better && Better != Target.Get())
		{
			AcquireTarget(Better, false);
		}
	}

	AActor* Current = Target.Get();
	const float Distance = FVector::Dist(Current->GetActorLocation(), MyPawn->GetActorLocation());
	const bool bSees = HasLineOfSightTo(Current);
	if (bSees)
	{
		LastSeenTime = Now;
		LastKnownTargetLocation = Current->GetActorLocation();
	}
	else
	{
		// Squad members never go looking on their own: they stay with the leader, who does.
		if (!FollowSquad())
		{
			HandleLostSight(Distance);
		}
		return;
	}

	const int32 Attack = SelectAttack(Distance, bSees);
	if (Attack != INDEX_NONE)
	{
		BeginAttack(Attack);
		return;
	}
	if (!FollowSquad())
	{
		UpdateMovement(Distance);
	}
}

// --- Squad -----------------------------------------------------------------------------------------------------------

void AFPSRLEnemyAIController::FormSquads(const TArray<AFPSRLEnemyAIController*>& Controllers)
{
	TArray<AFPSRLEnemyAIController*> Pool;
	for (AFPSRLEnemyAIController* Controller : Controllers)
	{
		if (Controller && Controller->GetPawn() && Controller->Profile && Controller->Profile->bSquadMovement)
		{
			Pool.Add(Controller);
		}
	}
	// Greedy: the first free member leads; the free members nearest to it (within SquadFormRadius) join, up to the size.
	while (!Pool.IsEmpty())
	{
		AFPSRLEnemyAIController* Leader = Pool[0];
		Pool.RemoveAt(0);
		const FVector Origin = Leader->GetPawn()->GetActorLocation();
		Pool.Sort([&Origin](const AFPSRLEnemyAIController& A, const AFPSRLEnemyAIController& B)
		{
			return FVector::DistSquared(A.GetPawn()->GetActorLocation(), Origin) < FVector::DistSquared(B.GetPawn()->GetActorLocation(), Origin);
		});
		TSharedPtr<FSquad> Squad = MakeShared<FSquad>();
		Squad->Members.Add(Leader);
		while (!Pool.IsEmpty() && Squad->Members.Num() < Leader->Profile->MaxSquadSize
			&& FVector::Dist(Pool[0]->GetPawn()->GetActorLocation(), Origin) <= Leader->Profile->SquadFormRadius)
		{
			Squad->Members.Add(Pool[0]);
			Pool.RemoveAt(0);
		}
		for (const TWeakObjectPtr<AFPSRLEnemyAIController>& Member : Squad->Members)
		{
			Member->Squad = Squad->Members.Num() > 1 ? Squad : nullptr;
		}
		if (Squad->Members.Num() > 1)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[AI] squad of %d led by %s"), Squad->Members.Num(), *GetNameSafe(Leader->GetPawn()));
		}
	}
}

bool AFPSRLEnemyAIController::IsAliveForSquad() const
{
	return GetPawn() && State != EFPSRLEnemyAIState::Dead && !(Health.IsValid() && Health->IsDead());
}

AFPSRLEnemyAIController* AFPSRLEnemyAIController::GetSquadLeader() const
{
	if (Squad)
	{
		for (const TWeakObjectPtr<AFPSRLEnemyAIController>& Member : Squad->Members)
		{
			if (Member.IsValid() && Member->IsAliveForSquad())
			{
				return Member.Get();
			}
		}
	}
	return const_cast<AFPSRLEnemyAIController*>(this);
}

int32 AFPSRLEnemyAIController::GetSquadSize() const
{
	int32 Alive = 0;
	if (Squad)
	{
		for (const TWeakObjectPtr<AFPSRLEnemyAIController>& Member : Squad->Members)
		{
			Alive += Member.IsValid() && Member->IsAliveForSquad() ? 1 : 0;
		}
	}
	return FMath::Max(1, Alive);
}

bool AFPSRLEnemyAIController::FollowSquad()
{
	AFPSRLEnemyAIController* Leader = GetSquadLeader();
	const APawn* MyPawn = GetPawn();
	const APawn* LeaderPawn = Leader ? Leader->GetPawn() : nullptr;
	if (!Squad || Leader == this || !MyPawn || !LeaderPawn)
	{
		return false;	// leads (or alone): its own movement
	}
	// Its spot: the members after the leader, in order, take places behind and beside it (a loose huddle that turns
	// with the leader), so the group moves as one and still leaves gaps to shoot through.
	int32 Slot = 0;
	for (const TWeakObjectPtr<AFPSRLEnemyAIController>& Member : Squad->Members)
	{
		if (Member.IsValid() && Member->IsAliveForSquad() && Member.Get() != Leader)
		{
			++Slot;
			if (Member.Get() == this)
			{
				break;
			}
		}
	}
	static const float SlotAngles[] = { 135.f, -135.f, 180.f, 90.f, -90.f, 155.f, -155.f };
	const float Angle = SlotAngles[(Slot - 1) % UE_ARRAY_COUNT(SlotAngles)];
	const float Radius = Profile->SquadSpacing * (1.f + (Slot - 1) / UE_ARRAY_COUNT(SlotAngles));
	const FRotator Facing(0.f, LeaderPawn->GetActorRotation().Yaw + Angle, 0.f);
	// Around where the leader will be in a moment, not where it is: members moving at the same speed otherwise trail.
	const FVector Ahead = LeaderPawn->GetVelocity().GetSafeNormal2D() * FMath::Min(LeaderPawn->GetVelocity().Size2D(), 600.f);
	FVector Spot = LeaderPawn->GetActorLocation() + Ahead + Facing.Vector() * Radius;
	ProjectToNav(Spot, Spot);

	const float FromSpot = FVector::Dist2D(MyPawn->GetActorLocation(), Spot);
	// While the leader walks the members walk with it; a standing leader lets them settle (regroup only when far off).
	const bool bLeaderMoving = LeaderPawn->GetVelocity().Size2D() > 50.f;
	if (FromSpot > (bLeaderMoving ? Profile->SquadSpacing * 0.5f : Profile->SquadRegroupDistance))
	{
		// Re-issue only when the spot moved on (the leader walked) or it isn't moving yet.
		if (!bMoving || FVector::Dist2D(Spot, LastSquadGoal) > Profile->SquadSpacing * 0.5f)
		{
			LastSquadGoal = Spot;
			MoveToSpot(Spot, Target.IsValid() ? EFPSRLEnemyAIState::Positioning : EFPSRLEnemyAIState::Idle);
		}
	}
	else if (bMoving && FromSpot < Profile->SquadSpacing * 0.5f)
	{
		StopMovement();
		bMoving = false;
	}
	if (!bMoving)
	{
		SetState(Target.IsValid() ? EFPSRLEnemyAIState::Positioning : EFPSRLEnemyAIState::Idle);
	}
	return true;
}

void AFPSRLEnemyAIController::HandleLostSight(float Distance)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Profile->LostSightResponse == EFPSRLLostSightResponse::SeekLastSeen)
	{
		// Seek (user: enemies out of sight must come looking, not stand still): another player in sight takes over;
		// otherwise walk to where the target was last seen, and from there keep hunting toward it. Never forgets it.
		if (AActor* Other = ChooseTarget(true); Other && Other != Target.Get() && HasLineOfSightTo(Other))
		{
			AcquireTarget(Other, false);
			return;
		}
		const bool bAtLastSeen = FVector::Dist2D(GetPawn()->GetActorLocation(), LastKnownTargetLocation) < 200.f;
		const FVector Goal = bAtLastSeen ? Target->GetActorLocation() : LastKnownTargetLocation;
		if (!bMoving || State != EFPSRLEnemyAIState::Pursuing || FVector::Dist2D(MoveGoal, Goal) > 300.f)
		{
			MoveToSpot(Goal, EFPSRLEnemyAIState::Pursuing);
		}
		return;
	}
	if (Now - LastSeenTime > Profile->TargetMemorySeconds)
	{
		// Forgotten: another visible player, else search / give up.
		AActor* Other = ChooseTarget(true);
		if (Other && Other != Target.Get() && HasLineOfSightTo(Other))
		{
			AcquireTarget(Other, false);
			return;
		}
		if (Profile->LostSightResponse != EFPSRLLostSightResponse::Search || State == EFPSRLEnemyAIState::Searching)
		{
			if (State != EFPSRLEnemyAIState::Searching || Now >= SearchEndTime)
			{
				UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s lost %s"), *GetNameSafe(GetPawn()), *GetNameSafe(Target.Get()));
				ClearTarget();
				SetState(EFPSRLEnemyAIState::Idle);
			}
			return;
		}
	}

	switch (Profile->LostSightResponse)
	{
	case EFPSRLLostSightResponse::Retarget:
		if (AActor* Other = ChooseTarget(true); Other && Other != Target.Get() && HasLineOfSightTo(Other))
		{
			AcquireTarget(Other, false);
			return;
		}
		// nobody else in sight: go after the last known position meanwhile
		MoveToSpot(LastKnownTargetLocation, EFPSRLEnemyAIState::Pursuing);
		break;
	case EFPSRLLostSightResponse::Pursue:
		MoveToSpot(Target->GetActorLocation(), EFPSRLEnemyAIState::Pursuing);
		break;
	case EFPSRLLostSightResponse::Search:
		if (State != EFPSRLEnemyAIState::Searching)
		{
			SearchEndTime = Now + Profile->SearchDuration;
			MoveToSpot(LastKnownTargetLocation, EFPSRLEnemyAIState::Searching);
		}
		break;
	default:	// Reposition: a spot in its band with sight of the target
		if (!bMoving || State != EFPSRLEnemyAIState::Repositioning)
		{
			FVector Spot;
			if (FindSpotAroundTarget(Profile->PreferredMinDistance, Profile->PreferredMaxDistance, true, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
			}
			else
			{
				MoveToSpot(LastKnownTargetLocation, EFPSRLEnemyAIState::Pursuing);
			}
		}
		break;
	}
}

void AFPSRLEnemyAIController::UpdateMovement(float Distance)
{
	AActor* Current = Target.Get();
	const APawn* MyPawn = GetPawn();
	const double Now = GetWorld()->GetTimeSeconds();
	const float Mid = (Profile->PreferredMinDistance + Profile->PreferredMaxDistance) * 0.5f;
	const FVector Away = (MyPawn->GetActorLocation() - Current->GetActorLocation()).GetSafeNormal2D();

	// GuardPosition: never beyond its guard radius; a target out of reach is only watched.
	if (Profile->MovementStyle == EFPSRLMovementStyle::GuardPosition)
	{
		const FVector Wanted = Current->GetActorLocation() + Away * Mid;
		const FVector FromHome = Wanted - HomeLocation;
		const FVector Goal = HomeLocation + FromHome.GetClampedToMaxSize(Profile->GuardRadius);
		if (FVector::Dist2D(Goal, MyPawn->GetActorLocation()) > Profile->AcceptanceRadius * 2.f)
		{
			MoveToSpot(Goal, Distance > Profile->PreferredMaxDistance ? EFPSRLEnemyAIState::Pursuing : EFPSRLEnemyAIState::Positioning);
		}
		else if (!bMoving)
		{
			SetState(EFPSRLEnemyAIState::Positioning);
		}
		return;
	}

	if (Distance > Profile->PreferredMaxDistance)
	{
		// Too far: approach to the middle of its band (Chase: all the way to its attack range).
		const bool bChase = Profile->MovementStyle == EFPSRLMovementStyle::Chase;
		const float Acceptance = bChase ? FMath::Max(Profile->AcceptanceRadius, Profile->PreferredMinDistance) : Mid;
		if (!bMoving || State != EFPSRLEnemyAIState::Pursuing)
		{
			bMoving = MoveToActor(Current, Acceptance, true, true, true) == EPathFollowingRequestResult::RequestSuccessful;
			MoveFailures += bMoving ? 0 : 1;
			SetState(EFPSRLEnemyAIState::Pursuing);
		}
		return;
	}

	if (Distance < Profile->PreferredMinDistance)
	{
		switch (Profile->TooCloseResponse)
		{
		case EFPSRLTooCloseResponse::HoldAndAttack:
			if (bMoving)
			{
				StopMovement();
				bMoving = false;
			}
			SetState(EFPSRLEnemyAIState::Positioning);
			return;
		case EFPSRLTooCloseResponse::Reposition:
		{
			FVector Spot;
			if ((!bMoving || State != EFPSRLEnemyAIState::Repositioning) && FindSpotAroundTarget(Profile->PreferredMinDistance, Profile->PreferredMaxDistance, true, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
			}
			return;
		}
		default:	// Retreat straight back to the middle of the band
		{
			FVector Spot;
			if ((!bMoving || State != EFPSRLEnemyAIState::Repositioning) && ProjectToNav(Current->GetActorLocation() + Away * Mid, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
			}
			return;
		}
		}
	}

	// In its band.
	if (State == EFPSRLEnemyAIState::Pursuing && bMoving)
	{
		StopMovement();	// arrived in range
		bMoving = false;
	}
	switch (Profile->MovementStyle)
	{
	case EFPSRLMovementStyle::Chase:
		if (!bMoving)
		{
			bMoving = MoveToActor(Current, FMath::Max(Profile->AcceptanceRadius, Profile->PreferredMinDistance), true, true, true) == EPathFollowingRequestResult::RequestSuccessful;
			SetState(EFPSRLEnemyAIState::Pursuing);
		}
		return;
	case EFPSRLMovementStyle::Strafe:
		if (!bMoving && Now >= NextRepositionTime)
		{
			NextRepositionTime = Now + Profile->RepositionInterval;
			const FVector Side = FVector::CrossProduct(Away, FVector::UpVector) * (FMath::RandBool() ? 1.f : -1.f);
			FVector Spot;
			if (ProjectToNav(MyPawn->GetActorLocation() + Side * Profile->StrafeDistance, Spot) && !IsCrowded(Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Positioning);
				return;
			}
		}
		break;
	case EFPSRLMovementStyle::Reposition:
		if (!bMoving && Now >= NextRepositionTime)
		{
			NextRepositionTime = Now + Profile->RepositionInterval;
			FVector Spot;
			if (FindSpotAroundTarget(Profile->PreferredMinDistance, Profile->PreferredMaxDistance, true, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
				return;
			}
		}
		break;
	case EFPSRLMovementStyle::Retreat:
		if (!bMoving && Distance < Profile->PreferredMaxDistance * 0.9f)
		{
			FVector Spot;
			if (ProjectToNav(Current->GetActorLocation() + Away * Profile->PreferredMaxDistance * 0.95f, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
				return;
			}
		}
		break;
	default:	// MaintainDistance: hold
		break;
	}
	if (!bMoving)
	{
		SetState(EFPSRLEnemyAIState::Positioning);
	}
}

// --- Movement helpers --------------------------------------------------------------------------------------------------

bool AFPSRLEnemyAIController::ProjectToNav(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Nav && Nav->ProjectPointToNavigation(Point, Projected, FVector(200.f, 200.f, 300.f)))
	{
		OutPoint = Projected.Location;
		return true;
	}
	return false;
}

bool AFPSRLEnemyAIController::IsCrowded(const FVector& Spot) const
{
	// Spacing: another living enemy (or where it's heading) too close to this spot.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (*It == GetPawn() || UFPSRLHealthComponent::IsPlayerSide(nullptr, *It) || !UFPSRLHealthComponent::IsPawnUp(*It))
		{
			continue;
		}
		const AFPSRLEnemyAIController* Other = Cast<AFPSRLEnemyAIController>(It->GetController());
		const FVector OtherSpot = Other && Other->bMoving ? Other->MoveGoal : It->GetActorLocation();
		if (FVector::Dist2D(OtherSpot, Spot) < Profile->SeparationRadius)
		{
			return true;
		}
	}
	return false;
}

bool AFPSRLEnemyAIController::FindSpotAroundTarget(float MinDistance, float MaxDistance, bool bNeedSight, FVector& OutSpot) const
{
	const AActor* Current = Target.Get();
	const APawn* MyPawn = GetPawn();
	if (!Current || !MyPawn)
	{
		return false;
	}
	// Sample spots in the band around the target (on the navmesh, spaced from other enemies, seeing the target if
	// needed); take the one nearest to where it stands.
	const FVector Center = Current->GetActorLocation();
	const float EyeHeight = GetEyeLocation().Z - MyPawn->GetActorLocation().Z;
	float BestTravel = MAX_flt;
	for (int32 Sample = 0; Sample < 16; ++Sample)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Radius = FMath::FRandRange(MinDistance, FMath::Max(MinDistance, MaxDistance));
		FVector Spot;
		if (!ProjectToNav(Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, Spot) || IsCrowded(Spot))
		{
			continue;
		}
		if (bNeedSight && !IsLineClear(Spot + FVector(0.f, 0.f, EyeHeight + 90.f), Center + FVector(0.f, 0.f, 40.f), Current))
		{
			continue;
		}
		const float Travel = FVector::Dist(Spot, MyPawn->GetActorLocation());
		if (Travel < BestTravel)
		{
			BestTravel = Travel;
			OutSpot = Spot;
		}
	}
	return BestTravel < MAX_flt;
}

bool AFPSRLEnemyAIController::MoveToSpot(const FVector& Spot, EFPSRLEnemyAIState MoveState)
{
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Spot, Profile->AcceptanceRadius, true, true, true, true);
	bMoving = Result == EPathFollowingRequestResult::RequestSuccessful;
	if (Result == EPathFollowingRequestResult::Failed)
	{
		++MoveFailures;
		if (MoveFailures == 1)
		{
			UE_LOG(LogFPSRL, Warning, TEXT("[AI] %s can't path to %s (no navmesh here?)"), *GetNameSafe(GetPawn()), *Spot.ToCompactString());
		}
	}
	MoveGoal = Spot;
	SetState(MoveState);
	return bMoving;
}

void AFPSRLEnemyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	bMoving = false;
	if (State == EFPSRLEnemyAIState::Searching && GetWorld()->GetTimeSeconds() < SearchEndTime)
	{
		// Look around the last known position.
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation Spot;
		if (Nav && Nav->GetRandomReachablePointInRadius(LastKnownTargetLocation, 500.f, Spot))
		{
			MoveToSpot(Spot.Location, EFPSRLEnemyAIState::Searching);
		}
	}
}

// --- Attacks -----------------------------------------------------------------------------------------------------------

AFPSRLWeapon* AFPSRLEnemyAIController::FindWeapon() const
{
	const APawn* MyPawn = GetPawn();
	for (TActorIterator<AFPSRLWeapon> It(GetWorld()); It && MyPawn; ++It)
	{
		if (It->GetOwner() == MyPawn && !It->IsHidden())
		{
			return *It;
		}
	}
	return nullptr;
}

int32 AFPSRLEnemyAIController::SelectAttack(float Distance, bool bSeesTarget) const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Current = Target.Get();
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Profile->Attacks.Num(); ++Index)
	{
		const FFPSRLEnemyAttack& Attack = Profile->Attacks[Index];
		if ((Best != INDEX_NONE && Attack.Priority <= Profile->Attacks[Best].Priority)
			|| Now < AttackReadyTimes[Index] || Distance < Attack.MinRange || Distance > Attack.MaxRange || (Attack.bRequiresLineOfSight && !bSeesTarget))
		{
			continue;
		}
		const FVector ToTarget = (Current->GetActorLocation() - MyPawn->GetActorLocation()).GetSafeNormal2D();
		if (FVector::DotProduct(MyPawn->GetActorForwardVector().GetSafeNormal2D(), ToTarget) < FMath::Cos(FMath::DegreesToRadians(Attack.MaxAngle)))
		{
			continue;	// still turning toward it
		}
		const bool bAvailable = Attack.Action == EFPSRLEnemyAttackAction::FireWeapon ? FindWeapon() != nullptr
			: Attack.Action == EFPSRLEnemyAttackAction::GameplayAbility ? AbilitySystem.IsValid() && Attack.AbilityTag.IsValid()
			: Attack.Action == EFPSRLEnemyAttackAction::LeapSlam ? PlanLeap(Attack, nullptr) : true;
		if (bAvailable)
		{
			Best = Index;
		}
	}
	return Best;
}

void AFPSRLEnemyAIController::BeginAttack(int32 AttackIndex)
{
	const FFPSRLEnemyAttack& Attack = Profile->Attacks[AttackIndex];
	CurrentAttack = AttackIndex;
	bExecuting = false;
	if (Attack.bHoldPosition && bMoving)
	{
		StopMovement();
		bMoving = false;
	}
	SetState(EFPSRLEnemyAIState::Attacking);
	// Players see it coming: the role's attack animation and telegraph, on every machine.
	if (UFPSRLEnemyRoleComponent* RoleView = GetPawn()->FindComponentByClass<UFPSRLEnemyRoleComponent>())
	{
		RoleView->StartAttack(Attack.Name, Attack.WindupSeconds, Target.Get(), Attack.bShowAimLine);
	}
	if (Attack.WindupSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::ExecuteAttack, Attack.WindupSeconds, false);
	}
	else
	{
		ExecuteAttack();
	}
}

void AFPSRLEnemyAIController::ExecuteAttack()
{
	APawn* MyPawn = GetPawn();
	if (CurrentAttack == INDEX_NONE || !MyPawn || State != EFPSRLEnemyAIState::Attacking)
	{
		return;
	}
	const FFPSRLEnemyAttack& Attack = Profile->Attacks[CurrentAttack];
	bExecuting = true;
	++AttacksExecuted;
	switch (Attack.Action)
	{
	case EFPSRLEnemyAttackAction::FireWeapon:
		if (AFPSRLWeapon* Weapon = FindWeapon())
		{
			Weapon->StartFiring();	// the weapon's own fire rate, spread, ammo and projectiles; it aims at GetAimPoint
		}
		break;
	case EFPSRLEnemyAttackAction::Melee:
	{
		const int32 Struck = FPSRLCombat::MeleeSweep(MyPawn, this, Attack.MaxRange, Attack.MeleeRadius, Attack.MeleeDamage, Attack.MeleeMaxTargets).Num();
		MeleeHits += Struck;
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s %s hits %d (target %.0f cm away)"), *MyPawn->GetName(), *Attack.Name.ToString(), Struck,
			Target.IsValid() ? FVector::Dist2D(Target->GetActorLocation(), MyPawn->GetActorLocation()) : -1.f);
	}
		break;
	case EFPSRLEnemyAttackAction::GameplayAbility:
		if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
		{
			ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(Attack.AbilityTag));
		}
		break;
	case EFPSRLEnemyAttackAction::LeapSlam:
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s leaps at %s"), *MyPawn->GetName(), *GetNameSafe(Target.Get()));
		StartLeap(Attack);
		return;	// its landing ends the execution
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s attacks %s with %s"), *MyPawn->GetName(), *GetNameSafe(Target.Get()), *Attack.Name.ToString());
	GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::EndAttackExecution, FMath::Max(0.01f, Attack.ExecuteSeconds), false);
}

void AFPSRLEnemyAIController::EndAttackExecution()
{
	if (CurrentAttack == INDEX_NONE)
	{
		return;
	}
	const FFPSRLEnemyAttack& Attack = Profile->Attacks[CurrentAttack];
	if (Attack.Action == EFPSRLEnemyAttackAction::FireWeapon)
	{
		if (AFPSRLWeapon* Weapon = FindWeapon())
		{
			Weapon->StopFiring();
		}
	}
	bExecuting = false;
	AttackReadyTimes[CurrentAttack] = GetWorld()->GetTimeSeconds() + Attack.Cooldown;
	SetState(EFPSRLEnemyAIState::Recovering);
	GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::FinishRecovery, FMath::Max(0.01f, Attack.RecoverySeconds), false);
}

void AFPSRLEnemyAIController::FinishRecovery()
{
	const bool bNearest = Profile && Profile->Attacks.IsValidIndex(CurrentAttack) && Profile->Attacks[CurrentAttack].bTargetNearestAfter;
	CurrentAttack = INDEX_NONE;
	if (AActor* Nearest = bNearest ? FindNearestPlayer() : nullptr)
	{
		AcquireTarget(Nearest, false);	// the Brute rushes whoever is closest after its leap
	}
	if (State == EFPSRLEnemyAIState::Recovering)
	{
		SetState(Target.IsValid() ? EFPSRLEnemyAIState::Positioning : EFPSRLEnemyAIState::Idle);
	}
}

void AFPSRLEnemyAIController::CancelAttack(float RecoverySeconds)
{
	if (CurrentAttack == INDEX_NONE)
	{
		return;
	}
	if (bExecuting && Profile->Attacks[CurrentAttack].Action == EFPSRLEnemyAttackAction::FireWeapon)
	{
		if (AFPSRLWeapon* Weapon = FindWeapon())
		{
			Weapon->StopFiring();
		}
	}
	bExecuting = false;
	GetWorldTimerManager().ClearTimer(AttackTimer);
	if (UFPSRLEnemyRoleComponent* RoleView = GetPawn() ? GetPawn()->FindComponentByClass<UFPSRLEnemyRoleComponent>() : nullptr)
	{
		RoleView->CancelAttack();
	}
	if (RecoverySeconds > 0.f)
	{
		SetState(EFPSRLEnemyAIState::Recovering);
		GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::FinishRecovery, RecoverySeconds, false);
	}
	else
	{
		CurrentAttack = INDEX_NONE;
	}
}

FVector AFPSRLEnemyAIController::GetAimPoint() const
{
	if (const AActor* Current = Target.Get())
	{
		FVector Aim = Current->GetActorLocation() + FVector(0.f, 0.f, 30.f);	// chest height
		if (!Profile)
		{
			return Aim;
		}
		// Where the target was a moment ago (moving keeps you ahead of its aim), then off by up to the spread cone.
		Aim -= Current->GetVelocity() * Profile->AimLagSeconds;
		const APawn* MyPawn = GetPawn();
		if (MyPawn && Profile->AimSpreadDegrees > 0.f)
		{
			const FVector Eye = MyPawn->GetPawnViewLocation();
			const FVector ToAim = Aim - Eye;
			Aim = Eye + FMath::VRandCone(ToAim.GetSafeNormal(), FMath::DegreesToRadians(Profile->AimSpreadDegrees)) * ToAim.Size();
		}
		return Aim;
	}
	const APawn* MyPawn = GetPawn();
	return MyPawn ? MyPawn->GetActorLocation() + MyPawn->GetActorForwardVector() * 1000.f : FVector::ZeroVector;
}

bool AFPSRLEnemyAIController::WantsToKeepFiring() const
{
	return bExecuting && CurrentAttack != INDEX_NONE && Profile && Profile->Attacks[CurrentAttack].Action == EFPSRLEnemyAttackAction::FireWeapon;
}

// --- Reactions ---------------------------------------------------------------------------------------------------------

void AFPSRLEnemyAIController::HandleDamaged(float Damage, APawn* Attacker)
{
	if (!Profile || !bEncounterActive || State == EFPSRLEnemyAIState::Dead || !Attacker || !IsValidTarget(Attacker))
	{
		return;
	}
	Threat.FindOrAdd(Attacker) += Damage;
	LastDamager = Attacker;
	if (!Target.IsValid())
	{
		if (Profile->bAggroOnDamage && State != EFPSRLEnemyAIState::Disabled)
		{
			AcquireTarget(Attacker, true);
		}
		return;
	}
	if (State == EFPSRLEnemyAIState::Disabled)
	{
		return;
	}
	switch (Profile->DamageResponse)
	{
	case EFPSRLDamageResponse::RetargetAttacker:
		AcquireTarget(Attacker, false);
		break;
	case EFPSRLDamageResponse::Interrupt:
		if (CurrentAttack != INDEX_NONE && Profile->Attacks[CurrentAttack].bInterruptible)
		{
			UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s interrupted"), *GetNameSafe(GetPawn()));
			CancelAttack(Profile->InterruptRecoverySeconds);
		}
		break;
	case EFPSRLDamageResponse::ContinuePursuit:
		if (CurrentAttack == INDEX_NONE && Target.IsValid())
		{
			bMoving = MoveToActor(Target.Get(), Profile->PreferredMinDistance, true, true, true) == EPathFollowingRequestResult::RequestSuccessful;
			SetState(EFPSRLEnemyAIState::Pursuing);
		}
		break;
	case EFPSRLDamageResponse::Reposition:
		if (CurrentAttack == INDEX_NONE)
		{
			FVector Spot;
			if (FindSpotAroundTarget(Profile->PreferredMinDistance, Profile->PreferredMaxDistance, true, Spot))
			{
				MoveToSpot(Spot, EFPSRLEnemyAIState::Repositioning);
			}
		}
		break;
	default:	// KeepAttacking
		break;
	}
}

void AFPSRLEnemyAIController::HandleStunTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (State == EFPSRLEnemyAIState::Dead || State == EFPSRLEnemyAIState::Inactive)
	{
		return;
	}
	if (NewCount > 0 && State != EFPSRLEnemyAIState::Disabled)
	{
		CancelAttack(0.f);
		StopMovement();
		bMoving = false;
		StateBeforeDisabled = Target.IsValid() ? EFPSRLEnemyAIState::Targeting : EFPSRLEnemyAIState::Idle;
		SetState(EFPSRLEnemyAIState::Disabled);
	}
	else if (NewCount <= 0 && State == EFPSRLEnemyAIState::Disabled)
	{
		SetState(StateBeforeDisabled);
	}
}

void AFPSRLEnemyAIController::HandleDeath(AController* KillerInstigator, AActor* Causer)
{
	CancelAttack(0.f);
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	GetWorldTimerManager().ClearTimer(AttackTimer);
	StopMovement();
	bMoving = false;
	ClearTarget();
	SetState(EFPSRLEnemyAIState::Dead);
}

void AFPSRLEnemyAIController::StartLeap(const FFPSRLEnemyAttack& Attack)
{
	ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	const AActor* Current = Target.Get();
	if (!EnemyCharacter || !Current)
	{
		EndAttackExecution();
		return;
	}
	StopMovement();
	bMoving = false;

	// The target may have moved behind cover during the roar: no clear arc now -> cancel and try again shortly.
	FVector Velocity;
	if (!PlanLeap(Attack, &Velocity))
	{
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s cancels its leap: no clear path to %s"), *EnemyCharacter->GetName(), *GetNameSafe(Current));
		const int32 LeapIndex = CurrentAttack;
		CancelAttack(0.f);
		if (AttackReadyTimes.IsValidIndex(LeapIndex))
		{
			AttackReadyTimes[LeapIndex] = GetWorld()->GetTimeSeconds() + 1.f;
		}
		++LeapsCancelled;
		return;
	}
	const float Seconds = Attack.LeapSeconds;
	BeginAirborne(EnemyCharacter);
	EnemyCharacter->LaunchCharacter(Velocity, true, true);
	bLeaping = true;
	EnemyCharacter->LandedDelegate.AddUniqueDynamic(this, &ThisClass::HandleLeapLanded);
	GetWorldTimerManager().SetTimer(LeapSafetyTimer, this, &ThisClass::FinishLeap, Seconds + 1.5f, false);
	if (UFPSRLEnemyRoleComponent* RoleView = EnemyCharacter->FindComponentByClass<UFPSRLEnemyRoleComponent>())
	{
		RoleView->PlayAction(FName(*(Attack.Name.ToString() + TEXT("_Execute"))), Seconds);
	}
}

void AFPSRLEnemyAIController::HandleLeapLanded(const FHitResult& Hit)
{
	EndAirborne();
	FinishLeap();
}

void AFPSRLEnemyAIController::BeginAirborne(ACharacter* EnemyCharacter)
{
	UCharacterMovementComponent* Movement = EnemyCharacter->GetCharacterMovement();
	if (SavedBrakingFalling < 0.f)
	{
		SavedBrakingFalling = Movement->BrakingDecelerationFalling;
		SavedFallingFriction = Movement->FallingLateralFriction;
	}
	Movement->BrakingDecelerationFalling = 0.f;
	Movement->FallingLateralFriction = 0.f;
	EnemyCharacter->LandedDelegate.AddUniqueDynamic(this, &ThisClass::HandleLeapLanded);
}

void AFPSRLEnemyAIController::EndAirborne()
{
	const ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	if (EnemyCharacter && SavedBrakingFalling >= 0.f)
	{
		EnemyCharacter->GetCharacterMovement()->BrakingDecelerationFalling = SavedBrakingFalling;
		EnemyCharacter->GetCharacterMovement()->FallingLateralFriction = SavedFallingFriction;
	}
	SavedBrakingFalling = -1.f;
}

void AFPSRLEnemyAIController::FinishLeap()
{
	GetWorldTimerManager().ClearTimer(LeapSafetyTimer);
	ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	EndAirborne();
	if (EnemyCharacter)
	{
		EnemyCharacter->LandedDelegate.RemoveDynamic(this, &ThisClass::HandleLeapLanded);
	}
	if (!bLeaping)
	{
		return;
	}
	bLeaping = false;
	if (!EnemyCharacter || !Profile || !Profile->Attacks.IsValidIndex(CurrentAttack) || Profile->Attacks[CurrentAttack].Action != EFPSRLEnemyAttackAction::LeapSlam)
	{
		return;	// cancelled in the air (stunned, died): no shockwave
	}
	// The shockwave from the floor under it, then the pause (the attack's recovery).
	const FFPSRLEnemyAttack& Attack = Profile->Attacks[CurrentAttack];
	const FVector Ground = EnemyCharacter->GetActorLocation() - FVector(0.f, 0.f, EnemyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	AFPSRLShockwave::Spawn(EnemyCharacter, Ground, Attack.ShockwaveDamage, Attack.ShockwaveRadius, Attack.ShockwaveSpeed, Attack.ShockwaveHeight);
	++LeapsLanded;
	EndAttackExecution();
}

AActor* AFPSRLEnemyAIController::FindNearestPlayer() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const APawn* MyPawn = GetPawn();
	AActor* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (const APlayerState* Entry : GameState && MyPawn ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		APawn* Player = Entry ? Entry->GetPawn() : nullptr;
		const float Distance = Player ? FVector::DistSquared(Player->GetActorLocation(), MyPawn->GetActorLocation()) : 0.f;
		if (Player && IsValidTarget(Player) && Distance < NearestDistance)
		{
			Nearest = Player;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}

void AFPSRLEnemyAIController::HandleProjectileHit(float Damage, APawn* Attacker)
{
	ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Profile || !Profile->bPhaseOnProjectileHit || !EnemyCharacter || !bEncounterActive || Now < NextPhaseTime
		|| State == EFPSRLEnemyAIState::Dead || State == EFPSRLEnemyAIState::Disabled || (Health.IsValid() && Health->IsDead()))
	{
		return;
	}
	NextPhaseTime = Now + Profile->PhaseSeconds + Profile->PhaseCooldown;
	++Phases;

	// Off its attack and its path; a leap back away from the shooter (still facing them: the animation jumps backwards).
	CancelAttack(0.f);
	StopMovement();
	bMoving = false;
	const FVector Away = Attacker ? (EnemyCharacter->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D() : -EnemyCharacter->GetActorForwardVector();
	const float Seconds = Profile->PhaseLeapSeconds;
	const float Gravity = EnemyCharacter->GetCharacterMovement()->GetGravityZ();
	BeginAirborne(EnemyCharacter);
	EnemyCharacter->LaunchCharacter(Away * (Profile->PhaseLeapDistance / Seconds) + FVector(0.f, 0.f, -0.5f * Gravity * Seconds), true, true);
	TWeakObjectPtr<UFPSRLEnemyRoleComponent> RoleView = EnemyCharacter->FindComponentByClass<UFPSRLEnemyRoleComponent>();
	if (RoleView.IsValid())
	{
		RoleView->SetPhased(true);
		RoleView->PlayAction(TEXT("Phase"), Seconds);
	}
	SetState(EFPSRLEnemyAIState::Recovering);
	GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::FinishRecovery, Seconds, false);
	GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [RoleView]()
	{
		if (RoleView.IsValid())
		{
			RoleView->SetPhased(false);
		}
	}), FMath::Max(0.05f, Profile->PhaseSeconds), false);
	UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s phases away from %s"), *EnemyCharacter->GetName(), *GetNameSafe(Attacker));
}

bool AFPSRLEnemyAIController::PlanLeap(const FFPSRLEnemyAttack& Attack, FVector* OutVelocity) const
{
	const ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	const AActor* Current = Target.Get();
	if (!EnemyCharacter || !Current)
	{
		return false;
	}
	// Lands where the target stands now (the navmesh under it), no farther than the attack reaches.
	const FVector Start = EnemyCharacter->GetActorLocation();
	const float Radius = EnemyCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = EnemyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Goal = Current->GetActorLocation();
	if (FVector::Dist2D(Start, Goal) > Attack.MaxRange)
	{
		Goal = Start + (Goal - Start).GetSafeNormal2D() * Attack.MaxRange + FVector(0.f, 0.f, Goal.Z - Start.Z);
	}
	FVector OnNav;
	if (ProjectToNav(Goal, OnNav))
	{
		Goal = OnNav + FVector(0.f, 0.f, HalfHeight);
	}
	// A ballistic arc that gets there in LeapSeconds.
	const float Seconds = Attack.LeapSeconds;
	const float Gravity = EnemyCharacter->GetCharacterMovement()->GetGravityZ();
	const FVector Delta = Goal - Start;
	const FVector Velocity(Delta.X / Seconds, Delta.Y / Seconds, Delta.Z / Seconds - 0.5f * Gravity * Seconds);

	// The whole arc must be clear of level geometry (walls, ceilings, pillars, ledges): its body swept along it, a bit
	// slimmer so brushing a floor or a corner doesn't count. Players and enemies don't block it.
	const FCollisionShape Body = FCollisionShape::MakeCapsule(Radius * 0.8f, HalfHeight * 0.8f);
	const FCollisionObjectQueryParams Geometry(FCollisionObjectQueryParams::InitType::AllStaticObjects);
	FCollisionQueryParams Params(TEXT("LeapArc"), false, EnemyCharacter);
	constexpr int32 Steps = 10;
	FVector Previous = Start;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Time = Seconds * Step / Steps;
		const FVector Point = Start + Velocity * Time + FVector(0.f, 0.f, 0.5f * Gravity * Time * Time);
		if (GetWorld()->SweepTestByObjectType(Previous, Point, FQuat::Identity, Geometry, Body, Params))
		{
			return false;
		}
		Previous = Point;
	}
	if (OutVelocity)
	{
		*OutVelocity = Velocity;
	}
	return true;
}
