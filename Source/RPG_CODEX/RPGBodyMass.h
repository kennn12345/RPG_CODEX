#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

/**
 * Realistic weight for every physics body of a humanoid (GASP / UE mannequin skeleton). The baseline for weight and
 * hit reactions: one table of each body part's share of total body weight (adult male segment fractions, de Leva 1996),
 * times the character's total weight. Change the total and every limb, joint and segment scales in proportion.
 *
 * For 200 lb: head 10.4, each neck piece 1.7, upper chest (spine_05) 14.4, chest (spine_04) 12.0, each collarbone 2.8,
 * spine_03 11.8, spine_02 11.0, lower back (spine_01) 9.9, pelvis 22.3, upper arm 5.4, forearm 3.2, hand 1.2,
 * thigh 28.3, below the knee (calf) 8.7, foot 2.7 lb (arms and legs each side).
 */
namespace RPGBodyMass
{
	/** Pounds -> kilograms. */
	constexpr float KgPerLb = 0.45359237f;

	/** Share of total body weight carried by this bone's body (0 if the table has no entry for it). */
	float Fraction(FName Bone);

	/** Sets every simulated body's mass on Mesh from the table and TotalKg (bodies the table doesn't know share what
	 *  is left over equally; the shares of known bodies are renormalized if some are missing). Returns the total set. */
	float Apply(USkeletalMeshComponent* Mesh, float TotalKg);

	/** The character's total: OverrideLbs if > 0, else the project's Humanoid Body Weight (RPG Power System settings). */
	float TotalKgFor(float OverrideLbs);
}
