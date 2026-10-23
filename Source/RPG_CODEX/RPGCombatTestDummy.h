#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGMeleeTarget.h"
#include "RPGPowerTypes.h"
#include "RPGCombatTestDummy.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UAnimSequence;
class UBlendSpace;
class URPGPhysicsReactionComponent;
class URPGCharacterStatsComponent;

/**
 * Mannequin to throw punches and kicks at. No AI. Its stats are the Power System's standard test bandit (180 lb,
 * low-moderate Strength and Durability, moderate Stability and Health). Each hit goes through the Power System: it
 * takes the damage, and flinches, staggers, is knocked down or launched by the result; after a ragdoll it gets up
 * where it landed. At 0 Health it is knocked out and revives (full Health) when it gets up. It wanders slowly around where it was placed (bWander),
 * blocks the player with its capsule, and its mesh's physics asset (PA_Mannequin: head, torso, arms, legs) is what the
 * melee hit sweeps detect. Hits are passed to its PhysicsReaction (partial physics / ragdoll); it stops while reacting.
 * It shows a hit counter.
 *
 * Place it from the Place Actors panel (search "Combat Test Dummy") or drag BP_CombatTestDummy into the level.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Combat Test Dummy"))
class RPG_CODEX_API ARPGCombatTestDummy : public AActor, public IRPGMeleeTarget
{
	GENERATED_BODY()

public:
	ARPGCombatTestDummy();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void ReceiveMeleeHit_Implementation(const FRPGMeleeHitInfo& Hit) override;
	virtual void ReceiveContactPush_Implementation(const FVector& PushVelocity, FName Bone) override;

	/** Blocks the player's movement. Not hit by the melee sweeps (those test the mesh's bodies). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Dummy")
	TObjectPtr<UCapsuleComponent> Capsule;

	/** Per-bone bodies from its physics asset. They only collide with the world (the floor) when simulating in a
	 *  reaction; pawns, the camera and its own capsule pass through. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Dummy")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** Physical hit reactions (PhysicsControl plugin). Turn off bReactionsEnabled for the old flinch instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Dummy")
	TObjectPtr<URPGPhysicsReactionComponent> PhysicsReaction;

	/** Health, Strength, Mass, Durability, Stability... (GAS attributes). Defaults are the standard test bandit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Dummy")
	TObjectPtr<URPGCharacterStatsComponent> Stats;

	/** Looping pose it stands in (empty = the mesh's reference pose). Used when it isn't wandering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy")
	TObjectPtr<UAnimSequence> IdleAnimation;

	// ---- Wandering ----

	/** Walk between random spots around where it was placed, pausing at each. Off = stands still (the old dummy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander")
	bool bWander = true;

	/** Idle / walk / run blend space played by speed (cm/s on its first axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** How far from its starting spot it may go, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "50"))
	float WanderRadius = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "10"))
	float WalkSpeed = 160.f;

	/** cm/s per second, speeding up and braking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "10"))
	float WalkAcceleration = 250.f;

	/** Degrees per second it turns toward where it is going. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "10"))
	float TurnRate = 160.f;

	/** Seconds it stands at each spot before setting off again (random between the two). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "0"))
	float PauseMin = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "0"))
	float PauseMax = 4.f;

	/** After being hit it stands still this long (on top of the physical reaction) before walking on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "0"))
	float HitPause = 1.5f;

	/** Highest ledge / step it walks up onto, cm (anything taller blocks it and it turns away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Wander", meta = (ClampMin = "0"))
	float MaxStepHeight = 15.f;

	/** Hits go through the Power System (damage, reaction and displacement from both sides' stats). Off = the older
	 *  prototype response below (full charge = ragdoll, a fixed shove); the physics reaction tests use that. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy")
	bool bUsePowerSystem = true;

	/** The last Power System result (Details panel while playing; tests read it). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Dummy")
	FRPGPowerResult LastPowerResult;

	/** Legacy (bUsePowerSystem off): the dummy is driven back along the strike (sliding, slowing at the Power System's
	 *  Shove Deceleration) by this many cm/s per unit of strike strength (1 = a typical tap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy", meta = (ClampMin = "0"))
	float ShoveSpeed = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy", meta = (ClampMin = "0"))
	float MaxShoveSpeed = 160.f;

	/** A limb held against it and still driving in (a kick extending into it) moves it back at this share of the
	 *  limb's speed into it: it gives way like something of its weight, instead of a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy", meta = (ClampMin = "0", ClampMax = "1"))
	float ContactPushShare = 0.35f;

	/** Temporary visual only: the mesh tips a few degrees away from the hit and springs back. Not a stagger.
	 *  Only used when the physics reaction is off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy")
	bool bFlinchOnHit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy", meta = (ClampMin = "0", EditCondition = "bFlinchOnHit"))
	float FlinchDegrees = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy", meta = (ClampMin = "0.05", EditCondition = "bFlinchOnHit"))
	float FlinchTime = 0.3f;

	/** Floating text above the dummy: hit count and the last hit (with rpg.Debug), and its name and Health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy")
	bool bShowHitText = true;

	/** Name shown above it and in the Power readout ("Heavy Brute"). Set: name and Health always show, even with
	 *  rpg.Debug off (the calibration arena's dummies). Empty: the actor's name, shown only with rpg.Debug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy")
	FString DisplayLabel;

	/** Get-up clips (from GASP), played where the body lies after a knockdown: lying face down / face up. Soft references,
	 *  loaded when first used: GASP clips pull in GASP Blueprints that need plugins not loaded yet at startup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpFaceDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpFaceUp;
	/** Stagger: a step or two back (GASP's backward walk-to-stop; the clip's own root motion carries the body, so the
	 *  feet stay planted), then it stands dazed for StaggerDazedTime before it walks on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Stagger")
	TSoftObjectPtr<UAnimSequence> StaggerBackAnimation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Stagger", meta = (ClampMin = "0"))
	float StaggerDazedTime = 1.f;
	/** Staggers step the way the blow pushes (one of GASP's eight stop-step directions, ending on either foot); a push
	 *  of at least this many cm (the Power System's displacement) takes the bigger running stop instead of the walking
	 *  one. StaggerBackAnimation is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Stagger", meta = (ClampMin = "0"))
	float StaggerHeavyDisplacement = 45.f;
	/** Where each clip's first frame lies in its own component space: pelvis X / Y (cm) and the pelvis-to-head direction
	 *  (degrees about up), measured from the clips. The dummy is turned and placed so that frame lands on the ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	FVector GetUpFaceDownStart = FVector(2.8f, -1.0f, 84.6f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	FVector GetUpFaceUpStart = FVector(0.1f, -35.0f, -92.1f);
	/** Lying on its left / right side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpLeftSide;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	TSoftObjectPtr<UAnimSequence> GetUpRightSide;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	FVector GetUpLeftSideStart = FVector(-5.9f, -5.5f, -7.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up")
	FVector GetUpRightSideStart = FVector(-4.7f, -13.6f, 160.6f);
	/** Get-up speed: this play rate at full health, down to GetUpHurtRate near 0 (and when knocked out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHealthyRate = 1.15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up", meta = (ClampMin = "0.1"))
	float GetUpHurtRate = 0.7f;
	/** On a slope the body starts tilted to the ground (up to this many degrees) and is upright by this fraction of
	 *  the get-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up", meta = (ClampMin = "0", ClampMax = "60"))
	float GetUpMaxTilt = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Dummy|Get Up", meta = (ClampMin = "0.05", ClampMax = "1"))
	float GetUpUprightAt = 0.55f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Dummy")
	int32 HitCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Dummy")
	FRPGMeleeHitInfo LastHit;

	/** Stops wandering and puts it back exactly where and how it was placed (tests use this for a repeatable start). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Dummy|Wander")
	void StopWanderingAtHome();

	/** Clears the hit counter and any flinch, and steps back to where it stood before any shoves (also a button in the
	 *  Details panel). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "RPG|Dummy")
	void ResetDummy();

private:
	/** Seconds of get-up or stagger clip left (the dummy does nothing else meanwhile but slide). */
	float GetUpTimeLeft = 0.f;
	bool bStaggering = false;
	float GetUpDuration = 0.f;
	FQuat GetUpTilt = FQuat::Identity;

	void PlayIdle();
	/** The stop-step clip for a stagger pushed along WorldPush (horizontal), heavy or not. */
	UAnimSequence* PickStaggerClip(const FVector& WorldPush, bool bHeavy) const;
	void UpdateWander(float DeltaTime);
	void PickWanderTarget();
	/** Keeps the capsule standing on the floor below it (falling off anything higher than a step). */
	void SnapToFloor(float DeltaTime);
	bool FindFloor(const FVector& At, float& OutFloorZ) const;
	bool TryStepUp(const FVector& Move);

	/** Power System hit: damage, reaction, shove, readout. */
	void ReceivePowerHit(const FRPGMeleeHitInfo& Hit);
	/** Hit-stop (attacker and dummy freeze briefly) and the attacking player's camera shake, by reaction. */
	void PlayImpactFeel(AActor* Attacker, ERPGPowerReaction Reaction);
	/** A ragdoll is about to stand back up: move the actor to where the body lies, so it gets up there. */
	void HandleLimpEnding();
	void SetHealthToMax();

	/** Health reached 0; revives when it gets up. */
	bool bKnockedOut = false;
	/** ResetDummy is putting it back home: don't move to the ragdoll. */
	bool bResetting = false;

	FTimerHandle HitStopTimer;
	TWeakObjectPtr<AActor> HitStopAttacker;

	FVector Home = FVector::ZeroVector;
	FRotator HomeRotation = FRotator::ZeroRotator;
	FVector WanderTarget = FVector::ZeroVector;
	bool bHasTarget = false;
	float Speed = 0.f;
	float FallSpeed = 0.f;
	/** Seconds left standing still (at a spot, or after a hit). */
	float PauseLeft = 0.f;
	bool bPlayingLocomotion = false;
	/** Which blend space axis is speed. */
	int32 SpeedAxis = 0;

	FVector MeshRestLocation = FVector::ZeroVector;
	FRotator MeshRestRotation = FRotator::ZeroRotator;
	/** Axis the flinch tips around, and time since the last hit (-1 = none). */
	FVector FlinchAxis = FVector::ZeroVector;
	FVector ShoveVelocity = FVector::ZeroVector;
	/** How far shoves have moved it since the last ResetDummy. */
	FVector ShoveOffset = FVector::ZeroVector;
	float FlinchElapsed = -1.f;
};
