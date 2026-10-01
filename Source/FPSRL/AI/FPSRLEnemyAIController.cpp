// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyAIController.h"
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
		HealthComponent->OnDeath.RemoveDynamic(this, &ThisClass::HandleDeath);
	}
	if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		ASC->RegisterGameplayTagEvent(FPSRLGameplayTags::Status_Stunned).Remove(StunHandle);
	}
	GetWorldTimerManager().ClearTimer(ThinkTimer);
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
		HandleLostSight(Distance);
		return;
	}

	const int32 Attack = SelectAttack(Distance, bSees);
	if (Attack != INDEX_NONE)
	{
		BeginAttack(Attack);
		return;
	}
	UpdateMovement(Distance);
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
			: Attack.Action == EFPSRLEnemyAttackAction::GameplayAbility ? AbilitySystem.IsValid() && Attack.AbilityTag.IsValid() : true;
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
		FPSRLCombat::MeleeSweep(MyPawn, this, Attack.MaxRange, Attack.MeleeRadius, Attack.MeleeDamage, Attack.MeleeMaxTargets);
		break;
	case EFPSRLEnemyAttackAction::GameplayAbility:
		if (UAbilitySystemComponent* ASC = AbilitySystem.Get())
		{
			ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(Attack.AbilityTag));
		}
		break;
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
	CurrentAttack = INDEX_NONE;
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
