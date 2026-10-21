// Power System calculator tests. No world needed: they call URPGPowerSystem::Calculate with hand-made inputs.
// Run: Tools > Session Frontend > Automation, filter "RPG.Power" (or -ExecCmds="Automation RunTests RPG.Power").

#include "RPGPowerSystem.h"
#include "RPGPowerSettings.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float CmPerFoot = 30.48f;

	/** A normal punch on the standard bandit (82 kg, Stability 50, Durability 50) at a Strength level. */
	FRPGPowerInput MakeStandardPunch(float Strength, int32 Seed = 1234)
	{
		FRPGPowerInput Input;
		Input.Attacker.Strength = Strength;
		Input.Target = FRPGPowerStats();
		Input.Target.Strength = 3.f;
		Input.HitBone = TEXT("spine_05");
		Input.StrikeDirection = FVector::ForwardVector;
		Input.RandomSeed = Seed;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerLadderTest, "RPG.Power.Ladder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerLadderTest::RunTest(const FString& Parameters)
{
	const URPGPowerSettings* Settings = URPGPowerSettings::Get();
	float PreviousMax = -1.f;

	for (const FRPGPowerLadderStep& Step : Settings->Ladder)
	{
		const FRPGPowerResult Result = URPGPowerSystem::Calculate(MakeStandardPunch(Step.Level));
		const float MaxFeet = Result.MaxDisplacement / CmPerFoot;

		TestEqual(FString::Printf(TEXT("Level %.0f: max displacement matches the ladder"), Step.Level), MaxFeet, Step.MaxDisplacementFeet, 0.01f);
		TestTrue(FString::Printf(TEXT("Level %.0f: rolled displacement within [roll min, max]"), Step.Level),
			Result.Displacement >= Result.MaxDisplacement * Settings->DisplacementRollMin - 0.01f && Result.Displacement <= Result.MaxDisplacement + 0.01f);
		TestTrue(FString::Printf(TEXT("Level %.0f: never less than the level below"), Step.Level), Result.MaxDisplacement >= PreviousMax);
		PreviousMax = Result.MaxDisplacement;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPowerChannelsTest, "RPG.Power.Channels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPGPowerChannelsTest::RunTest(const FString& Parameters)
{
	const URPGPowerSettings* Settings = URPGPowerSettings::Get();

	// Strength 1 barely does anything.
	{
		const FRPGPowerResult Weak = URPGPowerSystem::Calculate(MakeStandardPunch(1.f));
		TestEqual(TEXT("Strength 1: no stagger chance"), Weak.StaggerChance, 0.f);
		TestEqual(TEXT("Strength 1: no knockdown chance"), Weak.KnockdownChance, 0.f);
		TestEqual(TEXT("Strength 1: no displacement"), Weak.Displacement, 0.f);
		TestTrue(TEXT("Strength 1: flinch"), Weak.Reaction == ERPGPowerReaction::Flinch);
		TestTrue(TEXT("Strength 1: still deals some damage"), Weak.Damage > 0.f);
	}

	// Same seed = same result, so a preview matches the real hit.
	{
		const FRPGPowerResult A = URPGPowerSystem::Calculate(MakeStandardPunch(12.f, 77));
		const FRPGPowerResult B = URPGPowerSystem::Calculate(MakeStandardPunch(12.f, 77));
		TestEqual(TEXT("Preview: same displacement"), A.Displacement, B.Displacement);
		TestEqual(TEXT("Preview: same stagger"), A.bStagger, B.bStagger);
		TestEqual(TEXT("Preview: same knockdown"), A.bKnockdown, B.bKnockdown);
	}

	// Tiers: Power > Heavy > Normal.
	{
		FRPGPowerInput Input = MakeStandardPunch(8.f);
		const float Normal = URPGPowerSystem::Calculate(Input).EffectivePower;
		Input.Tier = ERPGPowerAttackTier::Heavy;
		Input.ChargePercent = 0.5f;
		const float Heavy = URPGPowerSystem::Calculate(Input).EffectivePower;
		Input.Tier = ERPGPowerAttackTier::Power;
		Input.ChargePercent = 1.f;
		const float Power = URPGPowerSystem::Calculate(Input).EffectivePower;
		TestTrue(TEXT("Heavy is stronger than normal"), Heavy > Normal);
		TestTrue(TEXT("Power is stronger than heavy"), Power > Heavy);
	}

	// A target twice as heavy feels half the power: a Strength 20 punch moves it like Strength 10 moves the bandit.
	{
		FRPGPowerInput Input = MakeStandardPunch(20.f);
		Input.Target.Mass = Settings->StandardMass * 2.f;
		const FRPGPowerResult Heavy = URPGPowerSystem::Calculate(Input);
		TestEqual(TEXT("Double mass: resistance 2"), Heavy.TargetResistance, 2.f, 0.001f);
		TestEqual(TEXT("Double mass: moves like half the level"), Heavy.MaxDisplacement / CmPerFoot, URPGPowerSystem::GetLadderDisplacementFeet(10.f), 0.01f);

		// Damage doesn't depend on how far the target moves.
		const FRPGPowerResult Standard = URPGPowerSystem::Calculate(MakeStandardPunch(20.f));
		TestEqual(TEXT("Mass doesn't change damage"), Heavy.Damage, Standard.Damage, 0.001f);
	}

	// Higher stability resists displacement; higher durability resists damage and injury.
	{
		FRPGPowerInput Input = MakeStandardPunch(14.f);
		const FRPGPowerResult Base = URPGPowerSystem::Calculate(Input);
		Input.Target.Stability = 80.f;
		Input.Target.Durability = 80.f;
		const FRPGPowerResult Tough = URPGPowerSystem::Calculate(Input);
		TestTrue(TEXT("Stability 80 moves less"), Tough.MaxDisplacement < Base.MaxDisplacement);
		TestTrue(TEXT("Durability 80 takes less damage"), Tough.Damage < Base.Damage);
		TestTrue(TEXT("Durability 80 is injured less"), Tough.StructuralDamage <= Base.StructuralDamage);
	}

	// Head hits deal more damage.
	{
		FRPGPowerInput Input = MakeStandardPunch(6.f);
		const float Body = URPGPowerSystem::Calculate(Input).Damage;
		Input.HitBone = TEXT("head");
		const float Head = URPGPowerSystem::Calculate(Input).Damage;
		TestEqual(TEXT("Head multiplier"), Head, Body * Settings->HeadDamageMultiplier, 0.001f);
	}

	// Strength 20 launches; Strength 8 (2.5 ft at most) only shoves.
	{
		const FRPGPowerResult Big = URPGPowerSystem::Calculate(MakeStandardPunch(20.f));
		TestTrue(TEXT("Strength 20: launch"), Big.Reaction == ERPGPowerReaction::Launch);
		TestTrue(TEXT("Strength 20: launch goes up"), Big.DisplacementVelocity.Z > 0.f);

		// The launch speed lands at the rolled distance on flat ground: range = v^2 sin(2a) / g.
		const float Angle = FMath::DegreesToRadians(Settings->LaunchAngle);
		const float Range = Big.DisplacementVelocity.SizeSquared() * FMath::Sin(2.f * Angle) / Settings->Gravity;
		TestEqual(TEXT("Strength 20: launch lands at the displacement"), Range, Big.Displacement, 1.f);

		const FRPGPowerResult Mid = URPGPowerSystem::Calculate(MakeStandardPunch(8.f));
		TestTrue(TEXT("Strength 8: not a launch"), Mid.Reaction != ERPGPowerReaction::Launch);
		TestEqual(TEXT("Strength 8: shove stays on the ground"), Mid.DisplacementVelocity.Z, 0.0, 0.001);
	}

	// Lethal preview.
	{
		FRPGPowerInput Input = MakeStandardPunch(20.f);
		Input.Target.Health = 1.f;
		TestTrue(TEXT("Predicts a lethal hit"), URPGPowerSystem::Calculate(Input).bWouldKill);
		Input.Target.Health = 100000.f;
		TestFalse(TEXT("Predicts a non-lethal hit"), URPGPowerSystem::Calculate(Input).bWouldKill);
	}

	return true;
}

#endif
