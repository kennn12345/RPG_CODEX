#include "RPGPowerSystem.h"

#include "RPGAttributeSet.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGPowerSettings.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr float CmPerFoot = 30.48f;

	/** 0 at A, 1 at B, clamped. */
	float Ramp(float Value, float A, float B)
	{
		return B > A ? FMath::Clamp((Value - A) / (B - A), 0.f, 1.f) : (Value >= B ? 1.f : 0.f);
	}

	const TCHAR* TierName(ERPGPowerAttackTier Tier)
	{
		switch (Tier)
		{
			case ERPGPowerAttackTier::Heavy:	return TEXT("Heavy");
			case ERPGPowerAttackTier::Power:	return TEXT("Power");
			default:							return TEXT("Normal");
		}
	}

	const TCHAR* ReactionName(ERPGPowerReaction Reaction)
	{
		switch (Reaction)
		{
			case ERPGPowerReaction::Stagger:	return TEXT("STAGGER");
			case ERPGPowerReaction::Knockdown:	return TEXT("KNOCKDOWN");
			case ERPGPowerReaction::Launch:		return TEXT("LAUNCH");
			default:							return TEXT("FLINCH");
		}
	}
}

float URPGPowerSystem::GetLadderDisplacementFeet(float Level)
{
	const TArray<FRPGPowerLadderStep>& Ladder = URPGPowerSettings::Get()->Ladder;
	if (Ladder.IsEmpty())
	{
		return 0.f;
	}

	if (Level <= Ladder[0].Level)
	{
		return Ladder[0].MaxDisplacementFeet;
	}

	for (int32 Index = 0; Index + 1 < Ladder.Num(); ++Index)
	{
		const FRPGPowerLadderStep& Low = Ladder[Index];
		const FRPGPowerLadderStep& High = Ladder[Index + 1];
		if (Level <= High.Level)
		{
			return FMath::Lerp(Low.MaxDisplacementFeet, High.MaxDisplacementFeet, Ramp(Level, Low.Level, High.Level));
		}
	}

	// Above the top rung: keep rising at the last rung's rate, so extraordinary power stays extraordinary.
	const FRPGPowerLadderStep& Last = Ladder.Last();
	if (Ladder.Num() < 2)
	{
		return Last.MaxDisplacementFeet;
	}
	const FRPGPowerLadderStep& BeforeLast = Ladder[Ladder.Num() - 2];
	const float Slope = (Last.MaxDisplacementFeet - BeforeLast.MaxDisplacementFeet) / FMath::Max(Last.Level - BeforeLast.Level, KINDA_SMALL_NUMBER);
	return Last.MaxDisplacementFeet + Slope * (Level - Last.Level);
}

FRPGPowerResult URPGPowerSystem::Calculate(const FRPGPowerInput& Input)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	FRandomStream Random(Input.RandomSeed != 0 ? Input.RandomSeed : FMath::Rand());
	FRPGPowerResult R;

	// ---- Attacker power, in ladder levels ----
	float TierPower = 1.f;
	if (Input.Tier == ERPGPowerAttackTier::Heavy)
	{
		TierPower = FMath::Lerp(S->HeavyPowerMin, S->HeavyPowerMax, FMath::Clamp(Input.ChargePercent, 0.f, 1.f));
	}
	else if (Input.Tier == ERPGPowerAttackTier::Power)
	{
		TierPower = S->PowerAttackPower;
	}
	const float SourcePower = Input.Source == ERPGPowerSource::Kick ? S->KickPower : 1.f;
	const float Momentum = FMath::Max(Input.ClosingSpeed, 0.f) / 100.f * S->MomentumLevelsPerMeterPerSecond;
	R.EffectivePower = Input.Attacker.Strength * TierPower * SourcePower + Momentum;

	// ---- Target resistance (to being moved) ----
	const float MassFactor = FMath::Pow(FMath::Max(Input.Target.Mass, 1.f) / S->StandardMass, S->MassExponent);
	const float StabilityFactor = FMath::Max(0.1f, 1.f + (Input.Target.Stability - 50.f) * S->StabilityPerPoint);
	R.TargetResistance = MassFactor * StabilityFactor;
	R.RelativePower = R.EffectivePower / FMath::Max(R.TargetResistance, KINDA_SMALL_NUMBER);

	// ---- Damage: from raw power, durability and hit location; independent of how far the target moves ----
	const float DurabilityFactor = FMath::Pow(50.f / FMath::Max(Input.Target.Durability, 1.f), S->DurabilityExponent);
	const FString Bone = Input.HitBone.ToString();
	const bool bHeadHit = Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("neck"));
	R.Damage = S->DamageScale * FMath::Pow(R.EffectivePower, S->DamageExponent) * DurabilityFactor * (bHeadHit ? S->HeadDamageMultiplier : 1.f);
	R.bWouldKill = Input.Target.Health > 0.f && R.Damage >= Input.Target.Health;

	// ---- Structural damage (injury potential) ----
	R.StructuralDamage = Ramp(R.EffectivePower * DurabilityFactor, S->InjuryStartLevel, S->InjuryCatastrophicLevel);

	// ---- Stagger ----
	const float StaggerFactor = FMath::Max(0.1f, 1.f + (Input.Target.StaggerResistance - 50.f) * S->StaggerResistancePerPoint);
	R.StaggerChance = Ramp(R.RelativePower / StaggerFactor, S->StaggerStartLevel, S->StaggerCertainLevel);
	R.bStagger = Random.FRand() < R.StaggerChance;

	// ---- Knockdown ----
	if (R.RelativePower >= S->KnockdownStartLevel)
	{
		R.KnockdownChance = FMath::Lerp(S->KnockdownStartChance, 1.f, Ramp(R.RelativePower, S->KnockdownStartLevel, S->KnockdownCertainLevel));
	}
	R.bKnockdown = Random.FRand() < R.KnockdownChance;

	// ---- Displacement: the ladder at the relative power, then a random share of it ----
	R.MaxDisplacement = FMath::Max(GetLadderDisplacementFeet(R.RelativePower), 0.f) * CmPerFoot;
	R.Displacement = R.MaxDisplacement * Random.FRandRange(S->DisplacementRollMin, 1.f);

	FVector Direction = Input.StrikeDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}

	const bool bLaunch = R.Displacement >= S->LaunchMinFeet * CmPerFoot;
	if (bLaunch)
	{
		// The launch speed whose arc plus slide covers Displacement in total (distance only grows with speed: bisect).
		float Low = 0.f, High = 1000.f;
		while (GetLaunchTotalDistance(High) < R.Displacement && High < 1.0e7f) High *= 2.f;
		for (int32 Step = 0; Step < 40; ++Step)
		{
			const float Mid = 0.5f * (Low + High);
			(GetLaunchTotalDistance(Mid) < R.Displacement ? Low : High) = Mid;
		}
		const float Speed = 0.5f * (Low + High);
		const float Angle = FMath::DegreesToRadians(S->LaunchAngle);
		R.DisplacementVelocity = Direction * (Speed * FMath::Cos(Angle)) + FVector::UpVector * (Speed * FMath::Sin(Angle));
		R.bKnockdown = true; // A launched target lands on the ground.
	}
	else if (R.Displacement > 0.f)
	{
		// Speed that slides to a stop after Displacement: on its feet, or as a ragdoll if knocked down.
		const float Deceleration = R.bKnockdown ? S->GetRagdollSlideDeceleration() : S->ShoveDeceleration;
		R.DisplacementVelocity = Direction * FMath::Sqrt(2.f * Deceleration * R.Displacement);
	}

	// ---- Summary: the strongest channel that fired ----
	if (bLaunch)				R.Reaction = ERPGPowerReaction::Launch;
	else if (R.bKnockdown)		R.Reaction = ERPGPowerReaction::Knockdown;
	else if (R.bStagger)		R.Reaction = ERPGPowerReaction::Stagger;
	else						R.Reaction = ERPGPowerReaction::Flinch;

	return R;
}

float URPGPowerSystem::GetLaunchTotalDistance(float Speed)
{
	const URPGPowerSettings* S = URPGPowerSettings::Get();
	const float Angle = FMath::DegreesToRadians(S->LaunchAngle);
	const float Horizontal = Speed * FMath::Cos(Angle);
	const float Vertical = Speed * FMath::Sin(Angle);
	// Time to come down LaunchDropHeight below where it started: Vertical*t - g/2*t^2 = -Drop (ragdoll gravity).
	const float G = S->GetRagdollGravity();
	const float AirTime = (Vertical + FMath::Sqrt(Vertical * Vertical + 2.f * G * S->LaunchDropHeight)) / G;
	return Horizontal * AirTime + Horizontal * Horizontal / (2.f * S->GetRagdollSlideDeceleration());
}

FRPGPowerStats URPGPowerSystem::ReadStats(AActor* Actor)
{
	FRPGPowerStats Stats;
	URPGCharacterStatsComponent* StatsComponent = URPGCharacterStatsComponent::FindStats(Actor);
	const URPGAttributeSet* A = StatsComponent ? StatsComponent->GetAttributes() : nullptr;
	if (A)
	{
		Stats.Strength = A->GetStrength();
		Stats.Mass = A->GetMass();
		Stats.Durability = A->GetDurability();
		Stats.Stability = A->GetStability();
		Stats.StaggerResistance = A->GetStaggerResistance();
		Stats.Health = A->GetHealth();
	}
	return Stats;
}

FRPGPowerInput URPGPowerSystem::MakeMeleeInput(const FRPGMeleeHitInfo& Hit)
{
	FRPGPowerInput Input;
	Input.Source = Hit.bKick ? ERPGPowerSource::Kick : ERPGPowerSource::Punch;
	Input.ChargePercent = Hit.ChargePercent;
	if (Hit.bCharged)
	{
		Input.Tier = Hit.ChargePercent >= 0.99f ? ERPGPowerAttackTier::Power : ERPGPowerAttackTier::Heavy;
	}

	Input.Attacker = ReadStats(Hit.Attacker);
	Input.Target = ReadStats(Hit.HitActor);
	Input.HitBone = Hit.HitBone;

	FVector Direction = Hit.StrikeDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() && Hit.Attacker)
	{
		Direction = (Hit.ImpactLocation - Hit.Attacker->GetActorLocation()).GetSafeNormal();
	}
	Input.StrikeDirection = Direction;

	if (Hit.Attacker && Hit.HitActor)
	{
		const FVector ToTarget = (Hit.HitActor->GetActorLocation() - Hit.Attacker->GetActorLocation()).GetSafeNormal2D();
		Input.ClosingSpeed = float(FVector::DotProduct(Hit.Attacker->GetVelocity(), ToTarget));
	}
	return Input;
}

FString URPGPowerSystem::DescribeResult(const FRPGPowerInput& Input, const FRPGPowerResult& Result)
{
	return FString::Printf(
		TEXT("POWER  %s %s  (Strength %.1f vs %.0f kg)  ->  %s\n")
		TEXT("  Effective Power %.1f   Target Resistance %.2f   Relative Power %.1f\n")
		TEXT("  Damage %.1f%s   Structural %.0f%%\n")
		TEXT("  Stagger %s (%.0f%%)   Knockdown %s (%.0f%%)\n")
		TEXT("  Displacement %.1f ft (max %.1f ft)   Velocity %.0f cm/s"),
		TierName(Input.Tier), Input.Source == ERPGPowerSource::Kick ? TEXT("Kick") : TEXT("Punch"),
		Input.Attacker.Strength, Input.Target.Mass, ReactionName(Result.Reaction),
		Result.EffectivePower, Result.TargetResistance, Result.RelativePower,
		Result.Damage, Result.bWouldKill ? TEXT(" (LETHAL)") : TEXT(""), Result.StructuralDamage * 100.f,
		Result.bStagger ? TEXT("YES") : TEXT("no"), Result.StaggerChance * 100.f,
		Result.bKnockdown ? TEXT("YES") : TEXT("no"), Result.KnockdownChance * 100.f,
		Result.Displacement / CmPerFoot, Result.MaxDisplacement / CmPerFoot, Result.DisplacementVelocity.Size());
}
