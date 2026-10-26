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

/**
 * A humanoid NPC on GASP's character setup (motion matching, foot placement): BP_RPGNpc is a copy of GASP's
 * SandboxCharacter_CMC reparented to this class, so it moves exactly like the player, walked by URPGNpcAIController.
 *
 * Hits (punches, kicks, energy spells) go through the Power System. Reactions stay realistic:
 *   - ordinary hits: the struck part gives on muscle springs on top of the animation (URPGPhysicsReactionComponent,
 *     whole body, capped physics weight so the animation never lets go) and recovers smoothly;
 *   - hits that really move the body: a balance step (one of GASP's eight-way stop steps, toward the push, on the hit
 *     leg's side) played as a montage, its root motion carrying the capsule so the feet stay planted;
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
	/** A push of at least this many cm (the Power System's displacement) takes a balance step; heavier pushes (at
	 *  StaggerHeavyDisplacement and up) take the bigger running stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float StepDisplacement = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float StaggerHeavyDisplacement = 45.f;
	/** How far a balance step carries the body: the step's own root motion is scaled to the push (this many cm per cm
	 *  of push), between StepMinDistance and StepMaxDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float StepPerDisplacement = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float StepMinDistance = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0"))
	float StepMaxDistance = 90.f;
	/** Energy spells: a charge of at least this much on a leg makes it step (lighter hits only give on the springs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Reactions", meta = (ClampMin = "0", ClampMax = "1"))
	float EnergyStepCharge = 0.25f;

	/** Get-up clips (GASP), lying face down / on its back / on its left / right side, and where each first frame lies
	 *  relative to the clip's root: pelvis X / Y (cm) and the pelvis-to-head direction (degrees), measured from the clips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpFaceDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpFaceUp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpLeftSide;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpRightSide;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	FVector GetUpFaceDownStart = FVector(2.8f, -10.7f, 84.6f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	FVector GetUpFaceUpStart = FVector(0.1f, 3.0f, -92.1f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	FVector GetUpLeftSideStart = FVector(-5.9f, -5.5f, -7.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up")
	FVector GetUpRightSideStart = FVector(-4.7f, -13.6f, 160.6f);
	/** Get-up speed: this rate at full health, down to GetUpHurtRate near 0 (and when knocked out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHealthyRate = 1.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|NPC|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHurtRate = 0.7f;

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
	/** A balance step toward Push (horizontal), the struck leg's side (+1 left, -1 right, 0 either), carrying Distance cm. */
	void PlayBalanceStep(const FVector& Push, bool bHeavy, float FootSide, float Distance);
	UAnimSequence* PickStepClip(const FVector& WorldPush, bool bHeavy, float FootSide) const;
	void PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction);
	void StopForReaction(float Seconds);
	void OnRagdoll();
	void HandleLimpEnding();
	void ApplyWalkIntent();

	FVector Home = FVector::ZeroVector;
	float BusyUntil = 0.f;
	bool bDown = false;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveReaction;
	FTimerHandle HitStopTimer;
	TWeakObjectPtr<AActor> HitStopAttacker;
};
