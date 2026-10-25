#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RPGStrikeContactComponent.h"
#include "RPGEnergySpellTypes.h"
#include "RPGDirectionalMovementTypes.h"
#include "RPGFlightAnimationTypes.h"
#include "RPGAnimInstance.generated.h"

/**
 * Proxy that evaluates the Animation Blueprint's graph unchanged, then turns the spine toward the attack aim:
 * yaw about the vertical and pitch about the (aimed) side axis, spread up the spine so it bends rather than
 * rotating as one block. Arms, guard and head ride on it, so punches travel where the player aims.
 * In first person the turn and tilt instead swing the upper chest (and the arms on it) about the eyes, like a view
 * model, so the hands keep their place in view while the player looks around, and never move toward the camera.
 * Last, strike contact (URPGStrikeContactComponent): a fist / foot that would pass a target's surface is stopped there and
 * its arm / leg re-solved with two-bone IK.
 */
struct FRPGAnimInstanceProxy : public FAnimInstanceProxy
{
	FRPGAnimInstanceProxy() = default;
	explicit FRPGAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

private:
	void ApplySpineAim(FPoseContext& Output) const;
	void ApplyGuardSteady(FPoseContext& Output) const;
	void ApplyStrikeContact(FPoseContext& Output);
	void ApplyFlight(FPoseContext& Output) const;
	void ApplyFlow(FPoseContext& Output) const;
    void ApplyRearSway(FPoseContext& Output) const;
    void ApplySpellPose(FPoseContext& Output) const;
    void ApplyDirectionalHover(FPoseContext& Output) const;

public:
	/** Component-space offset the contact moved this limb's hand / foot by in the last pose. */
	FRPGLimbContactResult GetContactResult(int32 Limb) const { return Limb >= 0 && Limb < RPGStrikeContact::NumLimbs ? ContactResults[Limb] : FRPGLimbContactResult(); }

private:

	float AimYaw = 0.f;
	float AimPitch = 0.f;
	bool bPitchAboutPivot = false;
	FVector PitchPivot = FVector::ZeroVector;
	float GuardSteady = 0.f;
	FVector GuardChestRest = FVector::ZeroVector;
	const UAnimMontage* GuardSteadyMontage = nullptr;
	FRPGLimbContactSet ContactSets[RPGStrikeContact::NumLimbs];
	/** Kept between poses: Applied grows at once (never into the surface), eases off over ContactReleaseTime. */
	FRPGLimbContactResult ContactResults[RPGStrikeContact::NumLimbs];
	float ContactReleaseTime = 0.06f;
	TArray<class UAnimSequence*> FlightClips;
	float FlightWeight = 0.f, FlightPhase = 0.f;
    float FlightTurn = 0.f, FlightActionTime = 0.f, FlightActionWeight = 0.f;
    int32 FlightActionIndex = INDEX_NONE;
	FVector FlightBlend = FVector::ZeroVector;

	TArray<FName> FlowBones;
	TArray<FVector> FlowRotations;
    FRPGFlightRearSway RearSway;
    FRPGEnergyPose SpellPose[2];
    FRPGDirectionalHoverPose DirectionalHover;
};

/**
 * Parent class of ABP_Unarmed (set by URPGPrototypeAnimationComponent::SetupAimAnimInstance). Everything the graph does
 * is untouched; URPGCombatAimComponent writes the aim here each frame and the proxy applies it after the graph.
 */
UCLASS(Transient, Blueprintable)
class RPG_CODEX_API URPGAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Torso turn toward the attack aim, degrees: + = to the character's right. Already clamped and blended. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	float AimYaw = 0.f;

	/** Torso pitch toward the attack aim, degrees: + = up. Already clamped and blended. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	float AimPitch = 0.f;

	/** First person: turn / tilt the upper chest about PitchPivot (the eyes, mesh space) instead of bending the spine. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	bool bPitchAboutPivot = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	FVector PitchPivot = FVector::ZeroVector;

	/**
	 * First person, guard up (written by URPGPrototypeAnimationComponent): share 0..1 of the way the shoulders are moved
	 * from where the locomotion carries the upper chest (spine_05) back to GuardChestRest, where the guard clip holds it
	 * (mesh space). Scaled by GuardSteadyMontage's blend weight, read when the pose is evaluated so it never lags the
	 * guard's own blend in / out by a frame.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	float GuardSteady = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> GuardSteadyMontage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	FVector GuardChestRest = FVector::ZeroVector;

	/** Contact surfaces per limb (RPGStrikeContact::ELimbIndex), world space, written by URPGStrikeContactComponent. */
	FRPGLimbContactSet ContactSets[RPGStrikeContact::NumLimbs];

	/** Seconds (time constant) the contact correction takes to ease off once nothing holds the limb back. */
	UPROPERTY(Transient)
	float ContactReleaseTime = 0.06f;

	/** What the contact did to this limb in the current pose (component space). */
	FRPGLimbContactResult GetContactResult(int32 Limb) const;

	/**
	 * Set on the first-person shadow body (a second copy of this Animation Blueprint): every frame, before it animates,
	 * it mirrors the player's prototype montages from this component with their third-person clips at the same time.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<class URPGPrototypeAnimationComponent> MirrorFrom;

	/** Follow-through (URPGFlightAnimationComponent): each bone turned by a rotation vector (component space, radians)
	 *  on top of the animation - limbs swinging with momentum and springing back. */
	TArray<FName> FlowBones;
	TArray<FVector> FlowRotations;
    FRPGFlightRearSway RearSway;
    FRPGEnergyPose SpellPose[2];
    FRPGDirectionalHoverPose DirectionalHover;

    /** Flight-only blend, copied to the worker-thread proxy in PreUpdate. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<class UAnimSequence>> FlightClips;
    float FlightWeight = 0.f;
    float FlightPhase = 0.f;
    float FlightTurn = 0.f, FlightActionTime = 0.f, FlightActionWeight = 0.f;
    int32 FlightActionIndex = INDEX_NONE;
    FVector FlightBlend = FVector::ZeroVector; // forward, boost, brake

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FRPGAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FRPGAnimInstanceProxy*>(InProxy); }

	friend struct FRPGAnimInstanceProxy;
};
