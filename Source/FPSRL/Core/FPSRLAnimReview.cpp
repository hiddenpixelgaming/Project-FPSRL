// Fill out your copyright notice in the Description page of Project Settings.

// Test only: FPSRL.AnimReview [pose 0-1]: for each enemy role (and the players' downed set), a row of its character in
// front of the camera, one per animation, frozen at the given point of the animation (default 0.45), and a screenshot
// per row ([AnimReview]). Used to check the retargeted animations (Tools/AnimImport) by eye.

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "FPSRL.h"

#if !UE_BUILD_SHIPPING
namespace FPSRLAnimReview
{
	struct FRow
	{
		const TCHAR* Role;
		const TCHAR* Mesh;
		TArray<FString> Anims;
	};

	static TArray<FRow> Rows()
	{
		const FString Enemies = TEXT("/Game/MainProject/Contents/Characters/Enemies/");
		auto Anims = [&Enemies](const TCHAR* Role, std::initializer_list<const TCHAR*> Names)
		{
			TArray<FString> Out;
			for (const TCHAR* Name : Names)
			{
				Out.Add(FString::Printf(TEXT("%s%s/Anims/%s_%s"), *Enemies, Role, Name, Role));
			}
			return Out;
		};
		TArray<FRow> Out;
		Out.Add({ TEXT("Grunt"), TEXT("/Game/Sci-FI_Troopers_Collection/SciFITrooper-01/SkeletalMesh/SK_SciFITrooper-01"),
			Anims(TEXT("Grunt"), { TEXT("MF_Rifle_Walk_Fwd"), TEXT("MM_Rifle_Fire"), TEXT("MM_HitReact_Front_Hvy_01"), TEXT("MM_Death_Front_01") }) });
		Out.Add({ TEXT("Brute"), TEXT("/Game/Sci-FI_Troopers_Collection/SciFITrooper-02/SkeletalMesh/SK_SciFiTrooperV2"),
			Anims(TEXT("Brute"), { TEXT("Mutant_Walking"), TEXT("Standing_Melee_Attack_Downward"), TEXT("Standing_Melee_Run_Jump_Attack"), TEXT("Standing_Taunt_Battlecry"), TEXT("MM_Attack_01") }) });
		Out.Add({ TEXT("Skirmisher"), TEXT("/Game/Sci-FI_Troopers_Collection/SciFITrooper_Girl_01/SkeletalMesh/SK_SciFiTrooperGirlV1"),
			Anims(TEXT("Skirmisher"), { TEXT("Sprint"), TEXT("Stabbing"), TEXT("Jump"), TEXT("MF_Unarmed_Jog_Fwd"), TEXT("MM_Dash") }) });
		Out.Add({ TEXT("Marksman"), TEXT("/Game/Sci-FI_Troopers_Collection/SciFITrooper_Girl_02/SkeletalMesh/SK_SciFiTrooperGirlV2"),
			Anims(TEXT("Marksman"), { TEXT("Idle_Aiming"), TEXT("Firing_Rifle"), TEXT("MF_Rifle_Walk_Fwd"), TEXT("MM_Rifle_Fire") }) });
		Out.Add({ TEXT("Downed"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"),
			{ TEXT("/Game/MainProject/Contents/Characters/Players/Downed/Falling_Down_Mannequin"), TEXT("/Game/MainProject/Contents/Characters/Players/Downed/Writhing_In_Pain_Mannequin"),
			  TEXT("/Game/MainProject/Contents/Characters/Players/Downed/Crawl_Forward_Mannequin"), TEXT("/Game/MainProject/Contents/Characters/Players/Downed/Standing_Up_Mannequin") } });
		return Out;
	}

	struct FState
	{
		int32 Row = 0;
		int32 Phase = 0;
		double PhaseTime = 0.0;
		float Pose = 0.45f;
		TArray<TWeakObjectPtr<AActor>> Spawned;
	};
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRLAnimReviewCommand(TEXT("FPSRL.AnimReview"),
	TEXT("Test: rows of each enemy role's character playing its animations (frozen at [pose 0-1]), one screenshot per row."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* StartWorld)
	{
		TWeakObjectPtr<UGameInstance> GameInstance = StartWorld ? StartWorld->GetGameInstance() : nullptr;
		TSharedRef<FPSRLAnimReview::FState> S = MakeShared<FPSRLAnimReview::FState>();
		S->Pose = Args.IsEmpty() ? 0.45f : FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 1.f);
		const TArray<FPSRLAnimReview::FRow> Rows = FPSRLAnimReview::Rows();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([GameInstance, S, Rows](float)
		{
			UGameInstance* GI = GameInstance.Get();
			UWorld* World = GI ? GI->GetWorld() : nullptr;
			APlayerController* PC = World ? GI->GetFirstLocalPlayerController(World) : nullptr;
			const ACharacter* Pawn = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (!Pawn)
			{
				return true;
			}
			if (!Rows.IsValidIndex(S->Row))
			{
				UE_LOG(LogFPSRL, Log, TEXT("[AnimReview] done"));
				return false;
			}
			const FPSRLAnimReview::FRow& Row = Rows[S->Row];
			if (S->Phase == 0)
			{
				FVector Eye;
				FRotator View;
				PC->GetPlayerViewPoint(Eye, View);
				const FRotator Yaw(0.f, View.Yaw, 0.f);
				const FVector Forward = Yaw.Vector();
				const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
				const float Feet = Pawn->GetActorLocation().Z - Pawn->GetSimpleCollisionHalfHeight();
				USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Row.Mesh);
				for (int32 Index = 0; Index < Row.Anims.Num(); ++Index)
				{
					UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *Row.Anims[Index]);
					const FVector Location = Eye + Forward * 650.f + Right * ((Index - (Row.Anims.Num() - 1) * 0.5f) * 210.f);
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(FVector(Location.X, Location.Y, Feet), FRotator(0.f, View.Yaw + 180.f - 90.f, 0.f), Params);
					if (!Actor || !Mesh || !Anim)
					{
						UE_LOG(LogFPSRL, Warning, TEXT("[AnimReview] %s: missing %s"), Row.Role, !Mesh ? Row.Mesh : *Row.Anims[Index]);
						continue;
					}
					USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
					Component->SetSkeletalMesh(Mesh);
					Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					Component->PlayAnimation(Anim, true);
					Component->SetPosition(Anim->GetPlayLength() * S->Pose, false);
					if (UAnimSingleNodeInstance* Single = Component->GetSingleNodeInstance())
					{
						Single->SetPlayRate(0.f);	// frozen on that frame for the screenshot
					}
					S->Spawned.Add(Actor);
					UE_LOG(LogFPSRL, Log, TEXT("[AnimReview] %s #%d: %s (%.2f s)"), Row.Role, Index + 1, *FPaths::GetBaseFilename(Row.Anims[Index]), Anim->GetPlayLength());
				}
				S->Phase = 1;
				S->PhaseTime = Now;
			}
			else if (S->Phase == 1 && Now - S->PhaseTime > 1.5)
			{
				PC->ConsoleCommand(TEXT("Shot"));
				UE_LOG(LogFPSRL, Log, TEXT("[AnimReview] screenshot: %s"), Row.Role);
				S->Phase = 2;
				S->PhaseTime = Now;
			}
			else if (S->Phase == 2 && Now - S->PhaseTime > 1.0)
			{
				for (const TWeakObjectPtr<AActor>& Actor : S->Spawned)
				{
					if (Actor.IsValid())
					{
						Actor->Destroy();
					}
				}
				S->Spawned.Reset();
				S->Phase = 0;
				++S->Row;
			}
			return true;
		}));
	}));
#endif
