#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UWorld;

/**
 * Developer-only Power System calibration arena. Console (while playing): rpg.Arena
 *
 * Builds a flat floor 100 m above the player (the test level is uneven: slopes and drops change how far launched
 * bodies travel), stands four dummies with different stats in a row in front of the player, and moves the player
 * there. Same punch, different targets:
 *   Standard Bandit  82 kg, Stability 50, Durability 40, Health 100 (the ladder's reference target)
 *   Light Scout      55 kg, Stability 40, Durability 35, Health 70
 *   Heavy Brute     164 kg, Stability 70, Durability 70, Health 250
 *   Steady Veteran   90 kg, Stability 85, Stagger Resistance 80, Durability 55, Health 150
 * Running it again moves the player back to the start of the arena.
 */
namespace RPGPowerArena
{
	/** A flat, collidable square floor (the engine cube, scaled) whose top surface is centred on Top. */
	RPG_CODEX_API AStaticMeshActor* BuildFlatFloor(UWorld* World, const FVector& Top, float SizeMeters);
}
