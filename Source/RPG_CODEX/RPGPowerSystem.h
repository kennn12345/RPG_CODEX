#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGPlayerTypes.h"
#include "RPGPowerTypes.h"
#include "RPGPowerSystem.generated.h"

/**
 * THE POWER SYSTEM: works out what an interaction can do. It only calculates; it never changes the world. The caller
 * applies the result (damage through GAS, reactions through the physics system), so the same calculation can preview
 * an attack, drive a melee hit now, and spells / projectiles / throws later.
 *
 * Pipeline: attacker power (Strength ladder level x attack tier x source + momentum) -> target resistance (mass,
 * stability) -> relative power -> independent channels: damage, structural damage, stagger, knockdown, displacement.
 * Displacement follows the Strength ladder in Project Settings > RPG Power System, indexed by relative power, so a
 * normal punch on the standard bandit reproduces the ladder exactly and heavier / steadier targets move less.
 *
 * Every number lives in URPGPowerSettings. Nothing here is final balance.
 */
UCLASS()
class RPG_CODEX_API URPGPowerSystem : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The full calculation. Pure: same input (and RandomSeed) = same result. */
	UFUNCTION(BlueprintPure, Category = "RPG|Power")
	static FRPGPowerResult Calculate(const FRPGPowerInput& Input);

	/** Builds the input for a melee hit: tier from the charge, both sides' stats, direction and closing speed. */
	UFUNCTION(BlueprintPure, Category = "RPG|Power")
	static FRPGPowerInput MakeMeleeInput(const FRPGMeleeHitInfo& Hit);

	/** An actor's current stats (defaults if it has no URPGCharacterStatsComponent). */
	UFUNCTION(BlueprintPure, Category = "RPG|Power")
	static FRPGPowerStats ReadStats(AActor* Actor);

	/** The ladder's maximum normal-punch displacement for a (possibly fractional) level, in feet. */
	UFUNCTION(BlueprintPure, Category = "RPG|Power")
	static float GetLadderDisplacementFeet(float Level);

	/** Total ground distance (cm) a body launched at this speed (cm/s) and LaunchAngle covers: the arc down to the
	 *  ground LaunchDropHeight below, plus the ragdoll's slide after landing. */
	static float GetLaunchTotalDistance(float Speed);

	/** Multi-line readout of every channel, for the debug display. */
	static FString DescribeResult(const FRPGPowerInput& Input, const FRPGPowerResult& Result);
};
