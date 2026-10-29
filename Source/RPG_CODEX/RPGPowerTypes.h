#pragma once

#include "CoreMinimal.h"
#include "RPGPowerTypes.generated.h"

/** Normal (tap), Heavy (partial charge) or Power (full charge). */
UENUM(BlueprintType)
enum class ERPGPowerAttackTier : uint8
{
	Normal,
	Heavy,
	Power
};

/** What delivered the force. Each source can convert power differently (lightning: damage, force blast: displacement). */
UENUM(BlueprintType)
enum class ERPGPowerSource : uint8
{
	Punch,
	Kick,
	/** An energy spell: its power comes from the charge (Energy Tap / Full Charge Level), not the caster's Strength. */
	Energy,
	/** Running into someone: classified by closing speed (Body Bumps settings); on foot never a knockdown. */
	Body
};

/** The overall physical reaction the result adds up to (the strongest channel that fired). */
UENUM(BlueprintType)
enum class ERPGPowerReaction : uint8
{
	/** Small visible response only. */
	Flinch,
	/** Interrupted, keeps its footing. */
	Stagger,
	/** Driven back several steps, still on its feet. */
	Knockback,
	/** Knocked off balance: stumbles with the arms out to break the fall and recovers (a failed catch is a Knockdown). */
	Catch,
	/** Loses its footing and goes down near where it stood. */
	Knockdown,
	/** Thrown through the air. */
	Launch
};

/** Where the body was struck, from the hit bone. Changes how the hit reads (head snaps back, body doubles over...). */
UENUM(BlueprintType)
enum class ERPGHitZone : uint8
{
	Body,
	Head,
	Arm,
	Leg
};

/** A snapshot of the stats the Power System reads. Taken from URPGAttributeSet; tests fill it by hand. */
USTRUCT(BlueprintType)
struct FRPGPowerStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Strength = 5.f;

	/** kg */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Mass = 82.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Durability = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Stability = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float StaggerResistance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Health = 100.f;
};

/** Everything one interaction needs. Built from a melee hit by URPGPowerSystem::MakeMeleeInput. */
USTRUCT(BlueprintType)
struct FRPGPowerInput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	ERPGPowerSource Source = ERPGPowerSource::Punch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	ERPGPowerAttackTier Tier = ERPGPowerAttackTier::Normal;

	/** 0-1, how far the attack was charged. Used within the Heavy tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float ChargePercent = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	FRPGPowerStats Attacker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	FRPGPowerStats Target;

	/** Body part that was hit (head, spine_05, thigh_l...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	FName HitBone;

	/** Direction the force travels (world space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	FVector StrikeDirection = FVector::ForwardVector;

	/** Attacker's speed toward the target at impact, cm/s (sprinting / diving attacks hit harder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float ClosingSpeed = 0.f;

	/** The target's balance when hit, 0-1 (URPGHitOutcomeComponent): a target already rocked by recent hits reacts more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Balance = 1.f;

	/** How the target's situation scales its reaction (planted in the stance < 1, mid-attack / running / hit from behind > 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	float Situation = 1.f;

	/** Same seed = same rolls, so a preview matches the real hit. 0 = pick a random seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Power")
	int32 RandomSeed = 0;
};

/**
 * What an interaction can do, one independent channel each. Several can fire at once (a punch can deal heavy damage,
 * injure an arm and barely move the target). Calculating a result changes nothing in the world: it can be used as a
 * preview (e.g. Skyfall checking for a lethal hit) and applied later.
 */
USTRUCT(BlueprintType)
struct FRPGPowerResult
{
	GENERATED_BODY()

	/** Attacker's power in Strength-ladder levels after tier, source and momentum. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float EffectivePower = 0.f;

	/** How hard the target is to move, 1 = the standard bandit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float TargetResistance = 1.f;

	/** EffectivePower against this target, in ladder levels (what the standard bandit would feel). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float RelativePower = 0.f;

	/** RelativePower as the target's footing feels it (balance, situation, hit location): picks stagger, knockback,
	 *  catch and knockdown. Damage and displacement use RelativePower. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float ReactionLevel = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	ERPGHitZone HitZone = ERPGHitZone::Body;

	// ---- Damage channel ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float Damage = 0.f;

	/** Damage would take the target to 0 Health. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	bool bWouldKill = false;

	// ---- Structural channel (not applied yet: no injury system) ----

	/** 0 = no injury, 1 = catastrophic (broken / destroyed limb). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float StructuralDamage = 0.f;

	// ---- Stagger / knockdown channels ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float StaggerChance = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	bool bStagger = false;

	/** Hard enough to drive the target back several steps (relative power at or above Knockback Level). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	bool bKnockback = false;

	/** Knocked off balance (reaction level in the catch band): tries to catch itself; bKnockdown = it falls while trying. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	bool bCatch = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float KnockdownChance = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	bool bKnockdown = false;

	// ---- Displacement channel ----

	/** The most this hit could move the target, cm (before the random roll). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float MaxDisplacement = 0.f;

	/** How far this hit moves the target, cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	float Displacement = 0.f;

	/** Velocity to give the target (cm/s): a flat shove that slides to a stop, or a launch arc for Launch. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	FVector DisplacementVelocity = FVector::ZeroVector;

	// ---- Summary ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Power")
	ERPGPowerReaction Reaction = ERPGPowerReaction::Flinch;
};
