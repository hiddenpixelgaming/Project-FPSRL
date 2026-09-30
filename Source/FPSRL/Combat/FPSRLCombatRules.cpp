// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/FPSRLCombatRules.h"
#include "Abilities/Attributes/FPSRLCombatSet.h"
#include "AbilitySystemComponent.h"
#include "Combat/FPSRLProjectile.h"
#include "Components/FPSRLBoonComponent.h"
#include "Components/FPSRLHealthComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Core/FPSRLPlayerController.h"
#include "Core/FPSRLPlayerState.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Types/FPSRLGameplayTags.h"
#include "FPSRL.h"

namespace FPSRLCombat
{
	FGameplayTag GetSourceTag(EFPSRLItemSource Source)
	{
		switch (Source)
		{
		case EFPSRLItemSource::Melee:	return FPSRLGameplayTags::Source_Melee;
		case EFPSRLItemSource::Ability:	return FPSRLGameplayTags::Source_Ability;
		default:						return FPSRLGameplayTags::Source_Ranged;
		}
	}

	FPlayerHit ResolvePlayerHit(AController* InstigatedBy, AActor* DamageCauser, AActor* Target, float& InOutDamage)
	{
		FPlayerHit Hit;
		APawn* Attacker = InstigatedBy ? InstigatedBy->GetPawn() : nullptr;
		if (!Attacker && DamageCauser)
		{
			Attacker = Cast<APawn>(DamageCauser);
			Attacker = Attacker ? Attacker : DamageCauser->GetInstigator();
		}
		AFPSRLPlayerState* State = Attacker ? Attacker->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		UAbilitySystemComponent* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
		if (!ASC || !State->GetBoonComponent())
		{
			return Hit;	// not a player (enemies' damage is left as it is)
		}
		Hit.AttackerASC = ASC;
		Hit.AttackerPawn = Attacker;
		Hit.AttackerState = State;
		Hit.Source = DamageCauser == Attacker ? EFPSRLItemSource::Melee : EFPSRLItemSource::Ranged;
		if (const AFPSRLProjectile* Projectile = Cast<AFPSRLProjectile>(DamageCauser))
		{
			Hit.AttackId = Projectile->AttackId;
		}
		else if (const AFPSRLPlayerController* PC = Hit.Source == EFPSRLItemSource::Melee ? Cast<AFPSRLPlayerController>(Attacker->GetController()) : nullptr)
		{
			Hit.AttackId = PC->GetCurrentMeleeAttackId();
		}

		auto Stat = [ASC](const FGameplayAttribute& Attribute) { return ASC->GetNumericAttribute(Attribute); };
		const bool bRanged = Hit.Source == EFPSRLItemSource::Ranged;
		const bool bMelee = Hit.Source == EFPSRLItemSource::Melee;
		const float SourceMultiplier = bRanged ? Stat(UFPSRLCombatSet::GetRangedDamageMultiplierAttribute())
			: bMelee ? Stat(UFPSRLCombatSet::GetMeleeDamageMultiplierAttribute()) : Stat(UFPSRLCombatSet::GetAbilityDamageMultiplierAttribute());
		const float CritChance = Stat(UFPSRLCombatSet::GetCritChanceAttribute())
			+ (bRanged ? Stat(UFPSRLCombatSet::GetRangedCritChanceAttribute()) : bMelee ? Stat(UFPSRLCombatSet::GetMeleeCritChanceAttribute()) : 0.f);
		Hit.bCritical = CritChance > 0.f && FMath::FRand() < CritChance;

		InOutDamage *= Stat(UFPSRLCombatSet::GetDamageMultiplierAttribute()) * SourceMultiplier;
		if (Hit.bCritical)
		{
			InOutDamage *= Stat(UFPSRLCombatSet::GetCritDamageMultiplierAttribute());
		}
		State->GetBoonComponent()->ModifyOutgoingDamage(Hit, Target, InOutDamage);	// conditional Blessing bonuses
		return Hit;
	}

	void SendHitEvents(const FPlayerHit& Hit, AActor* Target, float Damage, bool bKilled)
	{
		FGameplayEventData Payload;
		Payload.Instigator = Hit.AttackerPawn;
		Payload.Target = Target;
		Payload.EventMagnitude = Damage;
		Payload.InstigatorTags.AddTag(GetSourceTag(Hit.Source));
		if (Hit.bCritical)
		{
			Payload.TargetTags.AddTag(FPSRLGameplayTags::Hit_Critical);
		}
		// The Blessings read which attack this belongs to while the events are handled (synchronously).
		UFPSRLBoonComponent* Boons = Hit.AttackerState ? Hit.AttackerState->GetBoonComponent() : nullptr;
		const int32 PreviousAttack = Boons ? Boons->EventAttackId : INDEX_NONE;
		if (Boons)
		{
			Boons->EventAttackId = Hit.AttackId;
		}
		Payload.EventTag = FPSRLGameplayTags::Event_Hit;
		Hit.AttackerASC->HandleGameplayEvent(FPSRLGameplayTags::Event_Hit, &Payload);
		if (bKilled)
		{
			Payload.EventTag = FPSRLGameplayTags::Event_Kill;
			Hit.AttackerASC->HandleGameplayEvent(FPSRLGameplayTags::Event_Kill, &Payload);
		}
		if (Boons)
		{
			Boons->EventAttackId = PreviousAttack;
		}
	}

	int32 NewAttackId()
	{
		static int32 LastAttackId = 0;
		LastAttackId = LastAttackId == MAX_int32 ? 1 : LastAttackId + 1;
		return LastAttackId;
	}

	void NotifyAttack(APawn* Attacker, EFPSRLItemSource Source, int32 AttackId)
	{
		AFPSRLPlayerState* State = Attacker && Attacker->HasAuthority() ? Attacker->GetPlayerState<AFPSRLPlayerState>() : nullptr;
		UAbilitySystemComponent* ASC = State ? State->GetAbilitySystemComponent() : nullptr;
		UFPSRLBoonComponent* Boons = State ? State->GetBoonComponent() : nullptr;
		if (!ASC || !Boons || !Boons->ClaimAttack(Source, AttackId))
		{
			return;	// not a player, or this attack was already announced (another pellet of the same shot)
		}
		FGameplayEventData Payload;
		Payload.EventTag = FPSRLGameplayTags::Event_Attack;
		Payload.Instigator = Attacker;
		Payload.InstigatorTags.AddTag(GetSourceTag(Source));
		const int32 PreviousAttack = Boons->EventAttackId;
		Boons->EventAttackId = AttackId;
		ASC->HandleGameplayEvent(FPSRLGameplayTags::Event_Attack, &Payload);
		Boons->EventAttackId = PreviousAttack;
	}
}

namespace FPSRLCombat
{
	TArray<AActor*> MeleeSweep(APawn* Attacker, AController* InstigatedBy, float Range, float Radius, float Damage, int32 MaxTargets)
	{
		TArray<AActor*> Struck;
		UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
		if (!World || !Attacker->HasAuthority())
		{
			return Struck;
		}
		// Characters only: bullets in flight, props and walls must not use up the swing.
		const FVector Start = Attacker->GetActorLocation();
		const FVector End = Start + Attacker->GetActorForwardVector() * Range;
		TArray<FHitResult> Hits;
		UKismetSystemLibrary::SphereTraceMultiForObjects(Attacker, Start, End, Radius, { UEngineTypes::ConvertToObjectType(ECC_Pawn) },
			false, { Attacker }, EDrawDebugTrace::None, Hits, true);
		const bool bAttackerIsPlayer = UFPSRLHealthComponent::IsPlayerSide(InstigatedBy, Attacker);
		for (const FHitResult& Candidate : Hits)
		{
			AActor* Victim = Candidate.GetActor();
			const UFPSRLHealthComponent* Health = Victim ? Victim->FindComponentByClass<UFPSRLHealthComponent>() : nullptr;
			if (!Health || Health->IsDead() || Struck.Contains(Victim) || Struck.Num() >= MaxTargets
				|| UFPSRLHealthComponent::IsPlayerSide(nullptr, Victim) == bAttackerIsPlayer)
			{
				continue;
			}
			Struck.Add(Victim);
			UGameplayStatics::ApplyDamage(Victim, Damage, InstigatedBy, Attacker, nullptr);
		}
		return Struck;
	}
}
