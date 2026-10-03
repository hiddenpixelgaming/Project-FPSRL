// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.RoleTest (host, solo, Lobby; no god mode). For each enemy role (Brute, Skirmisher, Marksman) in turn:
// spawn it in front of the player and start it; check its setup (definition, speed, size, body, anim blueprint, the
// replicated role component) and that within a few seconds it attacks, plays its attack animation (melee roles), shows
// its aim line (Marksman) and actually hurts the player. Logs [RoleTest]; the player is healed between roles.

#include "AI/FPSRLEnemyAIController.h"
#include "AI/FPSRLEnemyAnimInstance.h"
#include "AI/FPSRLEnemyRoleComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/FPSRLHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/FPSRLEnemyScaling.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLRoleTest
{
	struct FRole
	{
		const TCHAR* Name;
		float Distance;
		bool bMeleeAnim;
		bool bAimLine;
	};
	static const FRole Roles[] = {
		{ TEXT("Brute"), 220.f, true, false },
		{ TEXT("Skirmisher"), 220.f, true, false },
		{ TEXT("Marksman"), 900.f, false, true },
		{ TEXT("NormalShooter"), 900.f, false, false },	// the Grunt: a control for the ranged checks
	};

	struct FState
	{
		int32 RoleIndex = 0;
		TWeakObjectPtr<ACharacter> Enemy;
		double StartTime = 0.0;
		float StartHealth = 0.f;
		bool bSawMontage = false;
		bool bSawAimLine = false;
		bool bShot = false;
		double AttackSeenTime = 0.0;
		int32 Problems = 0;
		void Check(bool bOk, const FString& What)
		{
			Problems += bOk ? 0 : 1;
			UE_LOG(LogFPSRL, Log, TEXT("[RoleTest] %s %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
	};
}

static FAutoConsoleCommandWithWorld GFPSRLRoleTestCommand(TEXT("FPSRL.RoleTest"),
	TEXT("Test (host, solo, Lobby): spawn each enemy role in front of the player and check it attacks and hurts ([RoleTest])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLRoleTest::FState> S = MakeShared<FPSRLRoleTest::FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			APawn* Player = PC ? PC->GetPawn() : nullptr;
			UFPSRLHealthComponent* PlayerHealth = Player ? Player->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!PlayerHealth || World->GetTimeSeconds() < 3.0)
			{
				return true;
			}
			const FPSRLRoleTest::FRole& Role = FPSRLRoleTest::Roles[S->RoleIndex];
			const FString Definition = FString::Printf(TEXT("/Game/MainProject/Contents/Data/Enemies/DA_Enemy_%s.DA_Enemy_%s"), Role.Name, Role.Name);
			const double Now = World->GetTimeSeconds();

			if (!S->Enemy.IsValid())
			{
				// Spawn this role in front of the player, facing it.
				const UFPSRLEnemyDefinition* Def = LoadObject<UFPSRLEnemyDefinition>(nullptr, *Definition);
				UClass* Class = Def ? Def->EnemyClass.LoadSynchronous() : nullptr;
				S->Check(Class != nullptr, FString::Printf(TEXT("%s: definition and class"), Role.Name));
				if (!Class)
				{
					return ++S->RoleIndex < UE_ARRAY_COUNT(FPSRLRoleTest::Roles);
				}
				PlayerHealth->Heal(10000.f);
				// The first direction around the player with a clear line (the Lobby has pedestals in the way).
				FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
				for (const float Yaw : { 0.f, 90.f, -90.f, 180.f, 45.f, -45.f, 135.f, -135.f })
				{
					const FVector Direction = Player->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(Yaw, FVector::UpVector);
					FHitResult Hit;
					FCollisionQueryParams Query(TEXT("RoleTest"), false, Player);
					if (!World->LineTraceSingleByChannel(Hit, Player->GetActorLocation() + Direction * Role.Distance, Player->GetActorLocation(), ECC_Visibility, Query))
					{
						Forward = Direction;
						break;
					}
				}
				const FTransform At((-Forward).Rotation(), Player->GetActorLocation() + Forward * Role.Distance);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				ACharacter* Enemy = World->SpawnActor<ACharacter>(Class, At, Params);
				if (Enemy && !Enemy->GetController())
				{
					Enemy->SpawnDefaultController();
				}
				AFPSRLEnemyAIController* AI = Enemy ? Cast<AFPSRLEnemyAIController>(Enemy->GetController()) : nullptr;
				S->Check(AI != nullptr, FString::Printf(TEXT("%s: spawned with its AI"), Role.Name));
				if (!AI)
				{
					return ++S->RoleIndex < UE_ARRAY_COUNT(FPSRLRoleTest::Roles);
				}
				// Its setup, from the definition.
				const UFPSRLEnemyDefinition* Found = UFPSRLEnemyScalingSettings::Get().FindDefinition(Class);
				S->Check(Found == Def, FString::Printf(TEXT("%s: definition found by class (%s)"), Role.Name, *GetNameSafe(Found)));
				S->Check(AI->GetProfile() == Def->BehaviorProfile, FString::Printf(TEXT("%s: behaviour profile %s"), Role.Name, *GetNameSafe(AI->GetProfile())));
				S->Check(Def->WalkSpeed <= 0.f || FMath::IsNearlyEqual(Enemy->GetCharacterMovement()->MaxWalkSpeed, Def->WalkSpeed),
					FString::Printf(TEXT("%s: speed %.0f (want %.0f)"), Role.Name, Enemy->GetCharacterMovement()->MaxWalkSpeed, Def->WalkSpeed));
				S->Check(FMath::IsNearlyEqual(Enemy->GetActorScale3D().Z, Def->Scale, 0.01f),
					FString::Printf(TEXT("%s: size %.2f (want %.2f)"), Role.Name, Enemy->GetActorScale3D().Z, Def->Scale));
				S->Check((Def->AttackAnims.IsEmpty() && !Role.bAimLine) || Enemy->FindComponentByClass<UFPSRLEnemyRoleComponent>() != nullptr, FString::Printf(TEXT("%s: role component"), Role.Name));
				const bool bArt = !Def->BodyMesh.IsNull() && Def->BodyMesh.LoadSynchronous();
				S->Check(!bArt || Enemy->FindComponentByTag<USkeletalMeshComponent>(TEXT("RoleBody")) != nullptr,
					FString::Printf(TEXT("%s: body %s"), Role.Name, bArt ? *Def->BodyMesh.GetAssetName() : TEXT("(art not on this machine)")));
				UClass* WantedAnim = Def->AnimSet.IsSet() ? UFPSRLEnemyAnimInstance::StaticClass() : Def->AnimClass.LoadSynchronous();
				UAnimInstance* Anim = Enemy->GetMesh()->GetAnimInstance();
				S->Check(!WantedAnim || (Anim && Anim->GetClass() == WantedAnim),
					FString::Printf(TEXT("%s: anim blueprint %s"), Role.Name, Anim ? *Anim->GetClass()->GetName() : TEXT("none")));
				AI->SetEncounterActive(true);
				S->Enemy = Enemy;
				S->StartTime = Now;
				S->StartHealth = PlayerHealth->GetCurrentHealth();
				S->bSawMontage = false;
				S->bSawAimLine = false;
				S->bShot = false;
				S->AttackSeenTime = 0.0;
				PC->SetControlRotation((Enemy->GetActorLocation() - Player->GetActorLocation()).Rotation() + FRotator(-5.f, 0.f, 0.f));
				return true;
			}

			ACharacter* Enemy = S->Enemy.Get();
			const AFPSRLEnemyAIController* AI = Cast<AFPSRLEnemyAIController>(Enemy->GetController());
			const UFPSRLEnemyRoleComponent* RoleView = Enemy->FindComponentByClass<UFPSRLEnemyRoleComponent>();
			const UAnimInstance* Anim = Enemy->GetMesh()->GetAnimInstance();
			S->bSawMontage |= Anim && (Anim->IsAnyMontagePlaying() || (Cast<UFPSRLEnemyAnimInstance>(Anim) && Cast<UFPSRLEnemyAnimInstance>(Anim)->IsPlayingAction()));
			S->bSawAimLine |= RoleView && RoleView->IsAimLineShowing();
			// Rendered runs: a screenshot mid-attack (wind-up animation or aim line).
			const bool bAttackShowing = (Role.bMeleeAnim && S->bSawMontage) || (Role.bAimLine && RoleView && RoleView->IsAimLineShowing());
			S->AttackSeenTime = S->AttackSeenTime > 0.0 || !bAttackShowing ? S->AttackSeenTime : Now;
			if (!S->bShot && S->AttackSeenTime > 0.0 && Now - S->AttackSeenTime > 0.3)	// into the swing / late in the aim
			{
				S->bShot = true;
				PC->ConsoleCommand(TEXT("Shot"));
			}
			const bool bHurt = PlayerHealth->GetCurrentHealth() < S->StartHealth;
			const bool bDone = bHurt && (!Role.bMeleeAnim || S->bSawMontage) && (!Role.bAimLine || S->bSawAimLine);
			if (!bDone && Now - S->StartTime < 10.0)
			{
				return true;
			}
			S->Check(AI && AI->GetAttacksExecuted() > 0, FString::Printf(TEXT("%s: attacked (%d)"), Role.Name, AI ? AI->GetAttacksExecuted() : 0));
			S->Check(bHurt, FString::Printf(TEXT("%s: hurt the player (%.0f -> %.0f)"), Role.Name, S->StartHealth, PlayerHealth->GetCurrentHealth()));
			if (Role.bMeleeAnim)
			{
				S->Check(S->bSawMontage, FString::Printf(TEXT("%s: attack animation played (%.2f s)"), Role.Name, RoleView ? RoleView->GetLastAnimLength() : 0.f));
			}
			if (Role.bAimLine)
			{
				S->Check(S->bSawAimLine, FString::Printf(TEXT("%s: aim line shown during the wind-up"), Role.Name));
			}
			UE_LOG(LogFPSRL, Log, TEXT("[RoleTest] %s took %.1f s"), Role.Name, Now - S->StartTime);
			if (AI)
			{
				AI->GetPawn()->Destroy();
			}
			Enemy->Destroy();
			PlayerHealth->Heal(10000.f);
			if (++S->RoleIndex < UE_ARRAY_COUNT(FPSRLRoleTest::Roles))
			{
				return true;
			}
			UE_LOG(LogFPSRL, Log, TEXT("[RoleTest] done: %d problem(s)"), S->Problems);
			return false;
		}));
	}));
#endif
