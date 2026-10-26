#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RPGMeleeTarget.h"
#include "RPGEnergySpellTypes.h"
#include "RPGPowerTypes.h"
#include "RPGNpcCharacter.generated.h"

class UAnimSequence;
class UAnimMontage;
class URPGCharacterStatsComponent;
class URPGPhysicsReactionComponent;
class URPGHitReactionComponent;
class URPGBodyImpactComponent;

/**
 * A humanoid NPC on GASP's character setup (motion matching, foot placement): BP_RPGNpc is a copy of GASP's
 * SandboxCharacter_CMC reparented to this class, so it moves exactly like the player, walked by URPGNpcAIController.
 *
 * Hits (punches, kicks, energy spells) go through the Power System. Reactions stay realistic:
 *   - ordinary hits: the body gives from the hit point (URPGHitReactionComponent: torque on every joint up to the hips,
 *     on the animation) and recovers smoothly;
 *   - and every hit pushes the body itself (force, direction, the attacker's momentum, its weight): it keeps facing
 *     its way while GASP's motion matching picks the footwork for that motion - a shuffle, a stumble, a sideways catch;
 *   - knockdown / launch / out of health: whole-body ragdoll, then a get-up chosen by how the body lies (face down,
 *     back, left or right side), placed where it lies.
 */
UCLASS()
class RPG_CODEX_API ARPGNpcCharacter : public ACharacter, public IRPGMeleeTarget, public IRPGEnergySpellTarget
{
	GENERATED_BODY()

public:
	ARPGNpcCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void ReceiveMeleeHit_Implementation(const FRPGMeleeHitInfo& Hit) override;
	virtual void ReceiveEnergyHit_Implementation(const FRPGEnergyImpact& Impact) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|NPC")
	TObjectPtr<URPGCharacterStatsComponent> Stats;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|NPC")
	TObjectPtr<URPGPhysicsReactionComponent> PhysicsReaction;
	/** Ordinary hits: the body's give, calculated from where and how hard it was hit and applied on the animation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|NPC")
	TObjectPtr<URPGHitReactionComponent> HitReaction;
	/** Body-to-body collisions (run into at speed: knocked down by momentum). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|NPC")
	TObjectPtr<URPGBodyImpactComponent> BodyImpact;

	/** Speed (cm/s) a flinch / stagger hit gives the struck body part; times that part's weight = the hit's push. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float HitSpeedFlinch = 160.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float HitSpeedStagger = 280.f;
	/** Share of the attacker's own momentum (their weight x speed) that carries into the hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float MomentumTransfer = 0.04f;

	/** Name shown over its head with rpg.Debug (empty: the actor's name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC")
	FString DisplayLabel;

	/** Wanders between random reachable spots within WanderRadius of where it was placed, pausing between walks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Wander")
	bool bWander = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Wander", meta = (ClampMin = "100"))
	float WanderRadius = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Wander", meta = (ClampMin = "0"))
	float WanderPauseMin = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Wander", meta = (ClampMin = "0"))
	float WanderPauseMax = 4.f;
	/** Walk rather than run while wandering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Wander")
	bool bWalk = true;

	/** Seconds it stands its ground after any hit before walking on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float HitPause = 1.2f;
	/** Pushes: a hit moves the body itself (the Power System's displacement velocity for melee, the spell's impulse over
	 *  the body's weight for energy, plus the attacker's momentum), and GASP's motion matching finds the footwork - a
	 *  shuffle, a stumble, a sideways catch - so no two pushes step the same. Melee: this x the displacement velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float ShoveScale = 2.5f;
	/** Energy: cm/s of push per kg cm/s of spell impulse (a full charge pushes ~2-3x a tap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float EnergyShovePerImpulse = 0.06f;
	/** Pushes slower than this (cm/s) don't move the feet; faster ones are capped at MaxShove. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float MinShove = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float MaxShove = 450.f;
	/** How quickly a pushed body brings itself to a stop (cm/s^2; lower = more steps), and for how long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "1"))
	float ShoveBraking = 380.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float ShoveTime = 1.1f;

	/** Get-up speed: this rate at full health, down to GetUpHurtRate near 0 (and when knocked out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHealthyRate = 1.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHurtRate = 0.7f;

	/** Total body weight (lb) spread over the body parts by RPGBodyMass; 0 = the project's Humanoid Body Weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC", meta = (ClampMin = "0"))
	float BodyWeightLbs = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|NPC")
	int32 HitCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|NPC")
	FRPGPowerResult LastPowerResult;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|NPC")
	bool bKnockedOut = false;

	/** Walking is off while it reacts, is down or is getting up (the AI controller waits). */
	bool IsBusy() const;
	FVector GetHome() const { return Home; }

private:
	void ReceivePowerHit(const FRPGMeleeHitInfo& Hit);
	/** Push the body at Velocity (world, horizontal): it keeps facing its way while GASP steps it to a stop. */
	void Shove(FVector Velocity);
	void PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction);
	void StopForReaction(float Seconds);
	void OnRagdoll();
	void HandleLimpEnding();
	void ApplyWalkIntent();
	/** The ordinary-hit give: push the struck part with Speed x its weight along Direction, plus the attacker's momentum. */
	void ReactToHit(FName Bone, const FVector& Point, const FVector& Direction, float Speed, const AActor* Attacker, float MaxImpulse = -1.f);

	FVector Home = FVector::ZeroVector;
	float BusyUntil = 0.f;
	float ShoveUntil = 0.f;
	FVector ShoveVelocity = FVector::ZeroVector;
	bool bShoving = false;
	bool bDown = false;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveReaction;
	FTimerHandle HitStopTimer;
	TWeakObjectPtr<AActor> HitStopAttacker;
};
