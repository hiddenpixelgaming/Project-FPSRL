// Fill out your copyright notice in the Description page of Project Settings.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "AI/FPSRLShieldEncounterComponent.h"
#include "Combat/FPSRLGroundStrike.h"
#include "Combat/FPSRLProjectile.h"
#include "Combat/FPSRLLandmine.h"
#include "Rooms/FPSRLRoom.h"
#include "Rooms/FPSRLShieldPlatform.h"
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
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
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
	// Stuck: trying to walk but not getting anywhere (wedged on a column or a ledge) - drop the path and pick again. Not while
	// attacking (a stab on the run, a crowd round the player: the attack drives it).
	if (bMoving && State != EFPSRLEnemyAIState::Attacking && State != EFPSRLEnemyAIState::Recovering && MyPawn->GetVelocity().Size2D() < 20.f)
	{
		StuckSince = StuckSince < 0.0 ? Now : StuckSince;
		if (Now - StuckSince > 2.5)
		{
			const UPathFollowingComponent* Follow = GetPathFollowingComponent();
			const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
			const int32 NextPoint = Follow ? static_cast<int32>(Follow->GetNextPathIndex()) : -1;
			// What's in the way: its capsule swept 80 cm towards the next path point.
			FString Blocker = TEXT("-");
			const UCapsuleComponent* Capsule = MyPawn->FindComponentByClass<UCapsuleComponent>();
			if (Capsule && Path.IsValid() && Path->GetPathPoints().IsValidIndex(NextPoint))
			{
				const FVector From = MyPawn->GetActorLocation() + FVector(0.f, 0.f, 50.f);	// above step height: not the floor it stands on
				const FVector Toward = (Path->GetPathPoints()[NextPoint].Location - From).GetSafeNormal2D();
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSRLEnemyStuck), false, MyPawn);
				Blocker = GetWorld()->SweepSingleByChannel(Hit, From, From + Toward * 80.f, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params)
					? FString::Printf(TEXT("%s (%s) at %.0f cm"), Hit.GetActor() ? *Hit.GetActor()->GetActorNameOrLabel() : TEXT("-"), Hit.GetComponent() ? *Hit.GetComponent()->Bounds.GetBox().ToString() : TEXT("-"), Hit.Distance)
					: TEXT("nothing");
				const ACharacter* Body = Cast<ACharacter>(MyPawn);
				const UCharacterMovementComponent* Move = Body ? Body->GetCharacterMovement() : nullptr;
				Blocker += FString::Printf(TEXT(", capsule r%.0f h%.0f, walk speed %.0f, step %.0f, mode %d, floor %s"), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
					Move ? Move->MaxWalkSpeed : -1.f, Move ? Move->MaxStepHeight : -1.f, Move ? static_cast<int32>(Move->MovementMode) : -1, Move ? *GetNameSafe(Move->CurrentFloor.HitResult.GetActor()) : TEXT("-"));
				// Where the navmesh puts it, and a fresh path from there to the end.
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
				FNavLocation OnNav;
				if (Nav && Nav->ProjectPointToNavigation(MyPawn->GetActorLocation(), OnNav, FVector(100.f, 100.f, 250.f)))
				{
					const UNavigationPath* Fresh = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), OnNav.Location, Path->GetPathPoints().Last().Location, MyPawn);
					Blocker += FString::Printf(TEXT(", on the navmesh at %s (%.0f cm off), a fresh path: %s%d point(s), %.0f cm"), *OnNav.Location.ToCompactString(),
						FVector::Dist2D(OnNav.Location, MyPawn->GetActorLocation()), Fresh && Fresh->IsPartial() ? TEXT("partial ") : TEXT(""),
						Fresh ? Fresh->PathPoints.Num() : 0, Fresh ? Fresh->GetPathLength() : 0.f);
					FBox Reach(ForceInit);
					for (int32 Sample = 0; Sample < 60; ++Sample)
					{
						FNavLocation Point;
						if (Nav->GetRandomReachablePointInRadius(OnNav.Location, 4000.f, Point))
						{
							Reach += Point.Location;
						}
					}
					Blocker += FString::Printf(TEXT(", reachable from here: %s"), *Reach.ToString());
					if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance()))
					{
						Blocker += FString::Printf(TEXT(", navmesh %s: radius %.0f height %.0f step %.0f slope %.0f cell %.0f/%.0f tile %.0f"), *Recast->GetName(), Recast->AgentRadius, Recast->AgentHeight,
							Recast->GetAgentMaxStepHeight(ENavigationDataResolution::Default), Recast->AgentMaxSlope, Recast->GetCellSize(ENavigationDataResolution::Default),
							Recast->GetCellHeight(ENavigationDataResolution::Default), Recast->TileSizeUU);
					}
				}
				else
				{
					Blocker += TEXT(", OFF the navmesh");
				}
			}
			// Stuck again where it was stuck a moment ago: a new path is the same path. Step aside to reachable ground first.
			const bool bAgain = FVector::Dist(MyPawn->GetActorLocation(), LastStuckLocation) < 150.f && Now - LastStuckTime < 10.0;
			LastStuckLocation = MyPawn->GetActorLocation();
			LastStuckTime = Now;
			UE_LOG(LogFPSRL, Log, TEXT("[AI] %s seems stuck at %s (%s): %s (follow %s, path %s%d point(s), next %s, end %s; in the way: %s)"), *MyPawn->GetName(),
				*MyPawn->GetActorLocation().ToCompactString(), *UEnum::GetValueAsString(State), bAgain ? TEXT("stepping aside") : TEXT("picking a new path"), Follow ? *UEnum::GetValueAsString(Follow->GetStatus()) : TEXT("-"),
				Path.IsValid() && Path->IsPartial() ? TEXT("partial ") : TEXT(""), Path.IsValid() ? Path->GetPathPoints().Num() : 0,
				Path.IsValid() && Path->GetPathPoints().IsValidIndex(NextPoint) ? *Path->GetPathPoints()[NextPoint].Location.ToCompactString() : TEXT("-"),
				Path.IsValid() && Path->GetPathPoints().Num() > 0 ? *Path->GetPathPoints().Last().Location.ToCompactString() : TEXT("-"), *Blocker);
			StopMovement();
			bMoving = false;
			StuckSince = -1.0;
			++TimesStuck;
			StuckRepeats = bAgain ? StuckRepeats + 1 : 0;
			if (StuckRepeats >= 2)
			{
				StuckRepeats = 0;
				HopAtTarget();
			}
			else if (bAgain)
			{
				StepAside(Path.IsValid() && Path->GetPathPoints().IsValidIndex(NextPoint) ? Path->GetPathPoints()[NextPoint].Location : MyPawn->GetActorLocation(),
					Path.IsValid() && Path->IsPartial());
			}
		}
	}
	else
	{
		StuckSince = -1.0;
	}
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
		// Safety net: nothing is driving the attack any more (no timer, not in the air) - never freeze in it.
		if (!GetWorldTimerManager().IsTimerActive(AttackTimer) && !bLeaping)
		{
			UE_LOG(LogFPSRL, Log, TEXT("[AI] %s was left in %s with nothing driving it: back to normal"), *GetNameSafe(GetPawn()), *UEnum::GetValueAsString(State));
			CurrentAttack = INDEX_NONE;
			bExecuting = false;
			SetState(Target.IsValid() ? EFPSRLEnemyAIState::Positioning : EFPSRLEnemyAIState::Idle);
		}
		else
		{
			return;	// the attack's timers drive these
		}
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
		// Attacks that don't need sight of the target still go (the Juggernaut's missiles and mines hit anyone anywhere).
		if (const int32 Blind = SelectAttack(Distance, false); Blind != INDEX_NONE)
		{
			BeginAttack(Blind);
			return;
		}
		if (bMoving && Now < SteppingAsideUntil)
		{
			return;	// stepping aside after being stuck
		}
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
	if (bMoving && Now < SteppingAsideUntil)
	{
		return;	// stepping aside after being stuck
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
	if (Profile->MovementStyle == EFPSRLMovementStyle::Stationary)
	{
		SetState(EFPSRLEnemyAIState::Positioning);
		return;	// never moves
	}
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
	if (Profile && Profile->MovementStyle == EFPSRLMovementStyle::Stationary)
	{
		SetState(MoveState == EFPSRLEnemyAIState::Idle ? MoveState : EFPSRLEnemyAIState::Positioning);
		return false;	// holds its ground (it still turns to aim)
	}
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
	if (bAttacksPaused)
	{
		return INDEX_NONE;	// an encounter mechanic is running (the Juggernaut's platforms)
	}
	const UFPSRLShieldEncounterComponent* Shield = MyPawn->FindComponentByClass<UFPSRLShieldEncounterComponent>();
	for (int32 Index = 0; Index < Profile->Attacks.Num(); ++Index)
	{
		const FFPSRLEnemyAttack& Attack = Profile->Attacks[Index];
		if (Attack.bOnlyWhileShielded && (!Shield || Shield->GetShieldsRemaining() <= 0))
		{
			continue;
		}
		if (Attack.Action == EFPSRLEnemyAttackAction::DeployMines && AFPSRLLandmine::GetMinesOf(MyPawn).Num() >= Attack.MaxActiveMines)
		{
			continue;	// at its cap: waits until one is set off
		}
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
	case EFPSRLEnemyAttackAction::GroundStrike:
		ExecuteGroundStrike(Attack);
		break;
	case EFPSRLEnemyAttackAction::DeployMines:
		DeployMines(Attack);
		break;
	case EFPSRLEnemyAttackAction::VolleyHeavy:
		UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s volley at %s"), *MyPawn->GetName(), *GetNameSafe(Target.Get()));
		VolleyShotsLeft = Attack.VolleyShots;
		bHeavyAiming = false;
		FireVolleyShot();
		return;	// its own timers end the execution
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
		// No recovery: straight back to its normal behaviour (left in Attacking it would never act again - a Brute whose
		// leap was cancelled stood frozen, playtest v0.1.45).
		if (State == EFPSRLEnemyAIState::Attacking || State == EFPSRLEnemyAIState::Recovering)
		{
			SetState(Target.IsValid() ? EFPSRLEnemyAIState::Positioning : EFPSRLEnemyAIState::Idle);
		}
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
	for (AFPSRLLandmine* Mine : AFPSRLLandmine::GetMinesOf(GetPawn()))
	{
		Mine->Destroy();
	}
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
	LeapGoal = EnemyCharacter->GetActorLocation() + Velocity * Seconds + FVector(0.f, 0.f, 0.5f * EnemyCharacter->GetCharacterMovement()->GetGravityZ() * Seconds * Seconds);
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
	// (No navmesh there - a test area: it lands where the target is.)
	FVector OnNav;
	const bool bOnNav = ProjectToNav(Goal, OnNav);
	// Not on top of another enemy, or where one is about to land (two Brutes landing together stack up and one ends on
	// whatever is beside them): round the target instead.
	const float Room = Radius * 2.f + 60.f;
	auto IsTaken = [this, Room](const FVector& Spot)
	{
		for (TActorIterator<AFPSRLEnemyAIController> It(GetWorld()); It; ++It)
		{
			const APawn* Other = *It != this ? It->GetPawn() : nullptr;
			if (Other && (FVector::Dist2D(Other->GetActorLocation(), Spot) < Room || (It->bLeaping && FVector::Dist2D(It->LeapGoal, Spot) < Room)))
			{
				return true;
			}
		}
		return false;
	};
	for (int32 Around = 0; bOnNav && IsTaken(OnNav); ++Around)
	{
		if (Around >= 8 || !ProjectToNav(Goal + FRotator(0.f, (Start - Goal).Rotation().Yaw + 45.f * Around, 0.f).Vector() * (Room + 60.f), OnNav))
		{
			return false;
		}
	}
	if (bOnNav)
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
	// Never onto ground it can't walk off (a crate or a ledge the navmesh doesn't join to the rest): it would be stranded
	// there once the player leaves. The landing must have a full path back to where it stands now.
	FVector Here;
	if (bOnNav && ProjectToNav(Start - FVector(0.f, 0.f, HalfHeight), Here))
	{
		const UNavigationPath* Back = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), OnNav, Here);
		if (!Back || !Back->IsValid() || Back->IsPartial())
		{
			return false;
		}
	}
	if (OutVelocity)
	{
		*OutVelocity = Velocity;
	}
	return true;
}

void AFPSRLEnemyAIController::SetAttacksPaused(bool bPaused)
{
	bAttacksPaused = bPaused;
	if (bPaused && CurrentAttack != INDEX_NONE)
	{
		CancelAttack(0.f);
	}
}

void AFPSRLEnemyAIController::ExecuteGroundStrike(const FFPSRLEnemyAttack& Attack)
{
	// A mark under each target's feet where they stand now (it doesn't follow them); StrikeTargets 0 = every player.
	APawn* MyPawn = GetPawn();
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	TArray<APawn*> Targets;
	for (const APlayerState* Entry : GameState ? GameState->PlayerArray : TArray<TObjectPtr<APlayerState>>())
	{
		APawn* Player = Entry ? Entry->GetPawn() : nullptr;
		if (Player && IsValidTarget(Player))
		{
			Targets.Add(Player);
		}
	}
	Targets.Sort([MyPawn](const APawn& A, const APawn& B) { return FVector::DistSquared(A.GetActorLocation(), MyPawn->GetActorLocation()) < FVector::DistSquared(B.GetActorLocation(), MyPawn->GetActorLocation()); });
	if (Attack.StrikeTargets > 0 && Targets.Num() > Attack.StrikeTargets)
	{
		Targets.SetNum(Attack.StrikeTargets);
	}
	for (const APawn* Player : Targets)
	{
		const ACharacter* PlayerCharacter = Cast<ACharacter>(Player);
		const float HalfHeight = PlayerCharacter ? PlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		FVector Ground = Player->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Player->GetActorLocation(), Player->GetActorLocation() - FVector(0.f, 0.f, 3000.f), ECC_Visibility,
			FCollisionQueryParams(TEXT("StrikeGround"), false, Player)))
		{
			Ground = Hit.ImpactPoint;	// a jumping player: the floor under them
		}
		AFPSRLGroundStrike::Spawn(MyPawn, Ground, Attack.StrikeRadius, Attack.StrikeWarningSeconds, Attack.StrikeDamage);
	}
	UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s %s: %d mark(s)"), *MyPawn->GetName(), *Attack.Name.ToString(), Targets.Num());
}

void AFPSRLEnemyAIController::DeployMines(const FFPSRLEnemyAttack& Attack)
{
	// Free floor around it: the navmesh's reachable ground in its arena (the room's bounds), at its own floor level (never
	// up on a platform), not under a player, not near the boss, and never inside another mine's reach.
	ACharacter* Boss = Cast<ACharacter>(GetPawn());
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Boss || !Nav)
	{
		return;
	}
	const AFPSRLRoom* Room = nullptr;
	for (TActorIterator<AFPSRLRoom> It(GetWorld()); It && !Room; ++It)
	{
		Room = It->IsInsideBounds(Boss->GetActorLocation()) ? *It : nullptr;
	}
	const FVector Center = Boss->GetActorLocation();
	const float Floor = Center.Z - Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const TArray<AFPSRLShieldPlatform*> Platforms = AFPSRLShieldPlatform::FindAround(GetWorld(), Center, 6000.f);
	TArray<FVector> Taken;
	for (const AFPSRLLandmine* Mine : AFPSRLLandmine::GetMinesOf(Boss))
	{
		Taken.Add(Mine->GetActorLocation());
	}
	const int32 ToPlace = FMath::Min(Attack.MinesPerDeploy, Attack.MaxActiveMines - Taken.Num());
	int32 Placed = 0;
	for (int32 Try = 0; Try < 40 && Placed < ToPlace; ++Try)
	{
		FNavLocation Point;
		if (!Nav->GetRandomReachablePointInRadius(Center, Attack.MinePlacementRadius, Point))
		{
			continue;
		}
		const FVector Spot = Point.Location;
		bool bOk = Spot.Z - Floor < 200.f && FVector::Dist2D(Spot, Center) > 500.f && (!Room || Room->IsInsideBounds(Spot));
		for (const AFPSRLShieldPlatform* Platform : Platforms)
		{
			const FVector Local = Platform->GetActorTransform().InverseTransformPosition(Spot);
			bOk &= !(FMath::Abs(Local.X) <= Platform->HalfSize.X + 100.f && FMath::Abs(Local.Y) <= Platform->HalfSize.Y + 100.f);
		}
		for (const FVector& Other : Taken)
		{
			bOk &= FVector::Dist2D(Spot, Other) > Attack.MineTriggerRadius * 2.f + 50.f;
		}
		for (TActorIterator<APawn> It(GetWorld()); It && bOk; ++It)
		{
			bOk &= !(UFPSRLHealthComponent::IsPlayerSide(nullptr, *It) && FVector::Dist2D(It->GetActorLocation(), Spot) < Attack.MineTriggerRadius + 250.f);
		}
		if (bOk && AFPSRLLandmine::Spawn(Boss, Spot, Attack.MineTriggerRadius, Attack.MineExplosionRadius, Attack.MineDamage, Attack.MineArmSeconds))
		{
			Taken.Add(Spot);
			++Placed;
		}
	}
	UE_LOG(LogFPSRL, Log, TEXT("[Juggernaut] %s laid %d mine(s), %d down now (max %d)"), *Boss->GetName(), Placed, Taken.Num(), Attack.MaxActiveMines);
}

void AFPSRLEnemyAIController::FireVolleyShot()
{
	// VolleyHeavy: the slow shots one by one, then the aim line, then the heavy shot (all on AttackTimer: a cancel stops it).
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !Profile || !Profile->Attacks.IsValidIndex(CurrentAttack) || !Target.IsValid())
	{
		EndAttackExecution();
		return;
	}
	const FFPSRLEnemyAttack& Attack = Profile->Attacks[CurrentAttack];
	if (VolleyShotsLeft > 0)
	{
		--VolleyShotsLeft;
		FireProjectileAtTarget(Attack, Attack.VolleyDamage, Attack.VolleySpeed);
		GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::FireVolleyShot, VolleyShotsLeft > 0 ? Attack.VolleyInterval : Attack.VolleyInterval * 1.5f, false);
		return;
	}
	if (!bHeavyAiming)
	{
		bHeavyAiming = true;
		if (UFPSRLEnemyRoleComponent* RoleView = MyPawn->FindComponentByClass<UFPSRLEnemyRoleComponent>())
		{
			RoleView->StartAttack(FName(*(Attack.Name.ToString() + TEXT("_Heavy"))), Attack.HeavyAimSeconds, Target.Get(), true);	// the laser
		}
		GetWorldTimerManager().SetTimer(AttackTimer, this, &ThisClass::FireVolleyShot, Attack.HeavyAimSeconds, false);
		return;
	}
	bHeavyAiming = false;
	FireProjectileAtTarget(Attack, Attack.HeavyDamage, Attack.HeavySpeed);
	UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s heavy shot at %s"), *MyPawn->GetName(), *GetNameSafe(Target.Get()));
	EndAttackExecution();
}

void AFPSRLEnemyAIController::FireProjectileAtTarget(const FFPSRLEnemyAttack& Attack, float Damage, float SpeedMultiplier)
{
	ACharacter* Shooter = Cast<ACharacter>(GetPawn());
	UClass* ProjectileClass = Attack.VolleyProjectile.LoadSynchronous();
	if (!Shooter || !ProjectileClass)
	{
		return;
	}
	// From its chest, a little in front of it, at where its aim says (dodgeable: slow, and aimed where the target was).
	const FVector From = Shooter->GetActorLocation() + FVector(0.f, 0.f, Shooter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.4f)
		+ Shooter->GetActorForwardVector() * (Shooter->GetCapsuleComponent()->GetScaledCapsuleRadius() + 60.f);
	const FTransform Shot((GetAimPoint() - From).Rotation(), From);
	if (AActor* Projectile = GetWorld()->SpawnActorDeferred<AActor>(ProjectileClass, Shot, Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		AFPSRLProjectile::SetProjectileDamage(Projectile, Damage);
		if (AFPSRLProjectile* Tracked = Cast<AFPSRLProjectile>(Projectile))
		{
			Tracked->SpeedMultiplier = SpeedMultiplier;
		}
		Projectile->FinishSpawning(Shot);
		++ProjectilesFired;
	}
}

void AFPSRLEnemyAIController::StepAside(const FVector& BlockedToward, bool bStranded)
{
	// A reachable spot 2-5 m away, not the way it was blocked; it walks there, then the chase picks up again.
	APawn* MyPawn = GetPawn();
	const UNavigationSystemV1* Nav = MyPawn ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	if (!Nav)
	{
		return;
	}
	if (bStranded && Target.IsValid())
	{
		// On ground the navmesh doesn't join to its target's (no full path): walk straight at the target, off the edge.
		if (MoveToLocation(Target->GetActorLocation(), 50.f, true, false, false, false, nullptr, true) == EPathFollowingRequestResult::RequestSuccessful)
		{
			bMoving = true;
			SteppingAsideUntil = GetWorld()->GetTimeSeconds() + 2.5;
			UE_LOG(LogFPSRL, Log, TEXT("[AI] %s is stranded: walks straight at %s"), *MyPawn->GetName(), *GetNameSafe(Target.Get()));
			return;
		}
	}
	const FVector Here = MyPawn->GetActorLocation();
	const FVector Blocked = (BlockedToward - Here).GetSafeNormal2D();
	for (int32 Try = 0; Try < 12; ++Try)
	{
		FNavLocation Point;
		if (!Nav->GetRandomReachablePointInRadius(Here, 500.f, Point))
		{
			continue;
		}
		const FVector Offset = Point.Location - Here;
		if (Offset.Size2D() < 200.f || (Try < 8 && FVector::DotProduct(Offset.GetSafeNormal2D(), Blocked) > 0.3f))
		{
			continue;
		}
		if (MoveToLocation(Point.Location, 50.f, true, true, false, false, nullptr, false) == EPathFollowingRequestResult::RequestSuccessful)
		{
			bMoving = true;
			SteppingAsideUntil = GetWorld()->GetTimeSeconds() + 2.5;
			UE_LOG(LogFPSRL, Verbose, TEXT("[AI] %s steps aside to %s"), *MyPawn->GetName(), *Point.Location.ToCompactString());
			return;
		}
	}
}

void AFPSRLEnemyAIController::HopAtTarget()
{
	// Stuck a third time in the same place (a pile-up, a ledge the navmesh doesn't see): a short hop at the target clears it.
	ACharacter* EnemyCharacter = Cast<ACharacter>(GetPawn());
	if (!EnemyCharacter || !Target.IsValid() || !EnemyCharacter->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	StopMovement();
	bMoving = false;
	const FVector Toward = (Target->GetActorLocation() - EnemyCharacter->GetActorLocation()).GetSafeNormal2D();
	EnemyCharacter->LaunchCharacter(Toward * 450.f + FVector(0.f, 0.f, 450.f), true, true);
	UE_LOG(LogFPSRL, Log, TEXT("[AI] %s hops at %s to get unstuck"), *EnemyCharacter->GetName(), *GetNameSafe(Target.Get()));
}
