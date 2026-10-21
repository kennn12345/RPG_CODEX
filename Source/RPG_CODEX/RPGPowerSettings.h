#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RPGPowerSettings.generated.h"

/** One rung of the Strength ladder. */
USTRUCT(BlueprintType)
struct FRPGPowerLadderStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float Level = 1.f;

	/** Farthest a normal punch at this level can move the standard bandit, in feet. A possibility, not a guarantee. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Power", meta = (ClampMin = "0"))
	float MaxDisplacementFeet = 0.f;
};

/**
 * Every Power System tuning value, in Edit > Project Settings > Game > RPG Power System. Changes save to
 * Config/DefaultGame.ini and apply the next time a hit is calculated: no recompiling.
 *
 * All values are provisional and meant to be calibrated in play.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "RPG Power System"))
class RPG_CODEX_API URPGPowerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URPGPowerSettings();

	static const URPGPowerSettings* Get() { return GetDefault<URPGPowerSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---------------- Strength ladder ----------------

	/** Normal punch against the standard bandit (82 kg, Stability 50) at each Strength level. Levels between rungs are
	 *  blended; above the top rung it keeps rising at the last rung's rate. */
	UPROPERTY(Config, EditAnywhere, Category = "Strength Ladder")
	TArray<FRPGPowerLadderStep> Ladder;

	/** Each hit moves the target a random share of its maximum, between this and 1, so the same punch varies. */
	UPROPERTY(Config, EditAnywhere, Category = "Strength Ladder", meta = (ClampMin = "0", ClampMax = "1"))
	float DisplacementRollMin = 0.6f;

	// ---------------- Attack modifiers (multiply effective power in ladder levels) ----------------

	UPROPERTY(Config, EditAnywhere, Category = "Attack Modifiers", meta = (ClampMin = "0"))
	float HeavyPowerMin = 1.15f;

	/** Heavy attacks blend from HeavyPowerMin to this by charge. */
	UPROPERTY(Config, EditAnywhere, Category = "Attack Modifiers", meta = (ClampMin = "0"))
	float HeavyPowerMax = 1.35f;

	/** Fully charged Power Attack. */
	UPROPERTY(Config, EditAnywhere, Category = "Attack Modifiers", meta = (ClampMin = "0"))
	float PowerAttackPower = 1.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Attack Modifiers", meta = (ClampMin = "0"))
	float KickPower = 1.15f;

	/** Extra ladder levels per m/s the attacker is moving toward the target (a sprint is about 8.5 m/s). */
	UPROPERTY(Config, EditAnywhere, Category = "Attack Modifiers", meta = (ClampMin = "0"))
	float MomentumLevelsPerMeterPerSecond = 0.15f;

	// ---------------- Target resistance ----------------

	/** The standard bandit's mass, kg. Targets this heavy have resistance 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Target Resistance", meta = (ClampMin = "1"))
	float StandardMass = 82.f;

	/** How strongly mass resists displacement: 1 = twice as heavy moves half as far. */
	UPROPERTY(Config, EditAnywhere, Category = "Target Resistance", meta = (ClampMin = "0"))
	float MassExponent = 1.f;

	/** Each Stability point above 50 resists displacement by this fraction (below 50 makes it easier). */
	UPROPERTY(Config, EditAnywhere, Category = "Target Resistance", meta = (ClampMin = "0"))
	float StabilityPerPoint = 0.01f;

	// ---------------- Stagger and knockdown (in relative ladder levels) ----------------

	/** Below this relative power: no stagger. */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown")
	float StaggerStartLevel = 3.f;

	/** At or above this: always staggers. */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown")
	float StaggerCertainLevel = 5.f;

	/** Each Stagger Resistance point above 50 lowers the stagger level felt by this fraction. */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown", meta = (ClampMin = "0"))
	float StaggerResistancePerPoint = 0.01f;

	/** Knockdown first becomes possible here ("occasional knockdown" at level 5)... */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown")
	float KnockdownStartLevel = 5.f;

	/** ...and is certain here. */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown")
	float KnockdownCertainLevel = 11.f;

	/** Chance of knockdown at KnockdownStartLevel (rises to 1 at KnockdownCertainLevel). */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown", meta = (ClampMin = "0", ClampMax = "1"))
	float KnockdownStartChance = 0.1f;

	/** Displacement at or beyond this (feet) is a launch through the air instead of a shove. */
	UPROPERTY(Config, EditAnywhere, Category = "Stagger and Knockdown", meta = (ClampMin = "0"))
	float LaunchMinFeet = 5.f;

	// ---------------- Damage ----------------

	/** Damage = DamageScale * EffectivePower ^ DamageExponent, then hit location and durability. */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0"))
	float DamageScale = 1.f;

	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0"))
	float DamageExponent = 1.6f;

	/** Hits on bones whose name contains "head" or "neck". */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0"))
	float HeadDamageMultiplier = 1.5f;

	/** Damage is multiplied by (50 / Durability) ^ this. 0 = durability ignored. */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0"))
	float DurabilityExponent = 0.5f;

	// ---------------- Structural damage (injury) ----------------

	/** Relative power (after durability) where injuries become possible ("limb injury potential" at level 10)... */
	UPROPERTY(Config, EditAnywhere, Category = "Structural Damage")
	float InjuryStartLevel = 10.f;

	/** ...and where they are catastrophic. */
	UPROPERTY(Config, EditAnywhere, Category = "Structural Damage")
	float InjuryCatastrophicLevel = 16.f;

	// ---------------- Displacement motion ----------------

	/** Launch angle above horizontal, degrees. Lower = flatter and faster (less hang time), for the same distance. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "1", ClampMax = "80"))
	float LaunchAngle = 15.f;

	/** Ragdolls from Power System hits fall this many times harder than normal gravity. Real-world gravity looks
	 *  floaty at game scale; games commonly use 1.5-2.5x. Launch speeds account for it, so distances still match. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "1", ClampMax = "4"))
	float RagdollGravityScale = 1.6f;

	/** cm/s^2. Gravity used to work out launch speed. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "1"))
	float Gravity = 980.f;

	/** cm/s^2 a shoved target that stays on its feet slides to a stop with. Must match how the target slows down. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "1"))
	float ShoveDeceleration = 500.f;

	/** cm/s^2 a ragdoll slows down sliding along the ground after it lands (or is knocked down sideways), at normal
	 *  gravity (scaled up with RagdollGravityScale). Measured on flat ground with the mannequin: about 1100. Launches
	 *  aim short by the slide, so the total matches the ladder. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "1"))
	float RagdollSlideDeceleration = 1100.f;

	/** How far the body's centre drops between the hit and lying on the ground, cm (standing pelvis ~95, lying ~15).
	 *  Launches account for the extra fall time. */
	UPROPERTY(Config, EditAnywhere, Category = "Displacement Motion", meta = (ClampMin = "0"))
	float LaunchDropHeight = 80.f;

	// ---------------- Impact feel ----------------

	/** A launched / knocked-down body doesn't move as one block: parts near the impact get up to (1 + this) times the
	 *  body's average velocity, far parts down to (1 - this), so it folds around the hit and the limbs whip after it.
	 *  The body's overall (average) motion is unchanged, so distances still match. 0 = moves as one block. */
	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0", ClampMax = "0.9"))
	float RagdollWhip = 0.5f;

	/** Distance from the impact point (cm) over which the whip fades from near to far. */
	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "1"))
	float RagdollWhipRadius = 90.f;

	/** Hit-stop: attacker and target freeze for this long on impact (seconds), by reaction. Sells the force of a hit.
	 *  0 = none. Console rpg.HitStop 0 turns it off. */
	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0", ClampMax = "0.5"))
	float HitStopStagger = 0.04f;

	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0", ClampMax = "0.5"))
	float HitStopKnockdown = 0.07f;

	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0", ClampMax = "0.5"))
	float HitStopLaunch = 0.1f;

	/** How slow time runs for both during hit-stop (0.02 = nearly frozen). */
	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0.001", ClampMax = "1"))
	float HitStopTimeDilation = 0.02f;

	/** Camera shake strength for the attacking player, by reaction (0 = none). */
	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0"))
	float ShakeStagger = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0"))
	float ShakeKnockdown = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Impact Feel", meta = (ClampMin = "0"))
	float ShakeLaunch = 1.f;

	/** Effective ragdoll gravity (cm/s^2). */
	float GetRagdollGravity() const { return Gravity * RagdollGravityScale; }

	/** Effective ragdoll slide deceleration: friction grows with the extra gravity. */
	float GetRagdollSlideDeceleration() const { return RagdollSlideDeceleration * RagdollGravityScale; }
};
