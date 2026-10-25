#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGFlightAnimationTypes.h"
#include "RPGFlightAnimationComponent.generated.h"

class UAnimSequence;

/** Continuous flight pose blending, aerodynamic body lean and bounded animation-side follow-through.
 * The ground animation graph and combat remain in control whenever the flight weight is zero.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGFlightAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGFlightAnimationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Hover;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Forward;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Fast;

	/** Nominal hover-to-forward speed (cm/s); the continuous blend begins at 60% of this value. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float ForwardSpeed = 150.f;

	/** Boost blend reference speed (cm/s): starts at 80%, fully streamlined at 145%. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float FastSpeed = 900.f;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float BlendTime = 0.45f;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
    TSoftObjectPtr<UAnimSequence> Brake;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> BankL;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> BankR;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> BankFastL;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> BankFastR;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> Takeoff;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> Accelerate;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> BoostClip;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Authored Clips")
    TSoftObjectPtr<UAnimSequence> Release;


    /** Speed at which the forward silhouette is fully extended (cm/s). */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "1"))
    float ForwardFullSpeed = 650.f;

    /** Smoothing of speed/pose changes, seconds. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
    float PoseResponseTime = 0.24f;

    /** Time to return to the ground/falling graph when flight is cancelled. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
    float ExitBlendTime = 0.9f;
    /** Let the catch/stride begin before blending it away into the falling graph. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0", ClampMax = "0.5"))
    float ReleasePoseHoldTime = 0.3f;
    /** Floor contact takes priority over the longer airborne release. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
    float GroundBlendOutTime = 0.16f;

    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.05"))
    float ActionBlendOutTime = 0.12f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.01"))
    float BrakeBlendInTime = 0.13f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0.01"))
    float BrakeBlendOutTime = 0.38f;

    /** Runtime blend diagnostics: hover->forward, boost and braking. */
    UPROPERTY(VisibleAnywhere, Category = "RPG|Flight Animation")
    FVector PoseBlend = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, Category = "RPG|Flight Animation")
    float FlightWeight = 0.f;

	/** Climb / dive: the body pitches by this fraction of the flight path's angle (up to MaxPitch). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float ClimbPitch = 1.f;
    /** Share of the flight-path pitch followed during ordinary cruise. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0", ClampMax = "1"))
    float CruisePathPitchScale = 1.f;
    /** Heading change in degrees per second that reaches the full authored corner pose. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "1"))
    float FullTurnPoseRate = 50.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float MaxPitch = 75.f;
	/** Degrees of lean per 1000 cm/s^2 of acceleration: forward when speeding up, back when slowing, sideways in turns. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float AccelLean = 0.f;
	/** Bank into turns: degrees per degree-per-second of turning, at FastSpeed (less when slower), up to MaxBank. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float BankPerTurn = 0.8f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float MaxBank = 45.f;
	/** How much of all this at a hover (1 at FastSpeed and above). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0", ClampMax = "1"))
	float HoverReaction = 1.f;
	/** How quickly the body follows (higher = snappier). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
	float ReactionSpeed = 8.f;
	/** Small, damped inertia on the upper arms and thighs. Authored clips own steering,
     *  shoulders and spine; no extra bracing or shoulder tilt is stacked onto them. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through")
	bool bLimbFlow = true;
	/** Pull back to the pose (higher = stiffer, quicker to return). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "1"))
	float FlowStiffness = 90.f;
	/** At FastSpeed the pull back is this fraction of FlowStiffness (harder to hold the pose at speed). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "1"))
	float FlowStiffnessAtSpeed = 0.8f;
	/** Bounce: lower = more overshoot and swinging back and forth, 1 = settles without overshoot. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "2"))
	float FlowDamping = 1.f;
	/** How hard momentum swings the limbs (per 1000 cm/s^2 of speeding up, slowing or turning). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowInertia = 2.5f;
	/** How hard the air sweeps them back (per 1000 cm/s, growing with speed). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowDrag = 0.8f;
	/** Largest swing of any joint, degrees. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowMaxAngle = 5.f;
	/** Turning swings the limbs too (they lag behind the body's turn). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowTurn = 0.08f;
    /** Lower-body follow-through, separate from the authored torso bank and the upper-arm springs. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "0.5"))
    float RearTurnLag = 0.16f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "15"))
    float RearSwayMaxAngle = 9.f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "1"))
    float RearSwayStiffness = 48.f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0.2", ClampMax = "2"))
    float RearSwayDamping = 0.85f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "12"))
    float LegFlowMaxAngle = 8.f;
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Rear Sway", meta = (ClampMin = "0", ClampMax = "6"))
    float RearKneeFlex = 3.5f;
    // Retained for serialized asset compatibility. Steering is authored in the
    // mirrored bank/brake clips; these former additive steering settings are inactive.
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlowBrace = 24.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlowBraceSharp = 8.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlowBraceMax = 65.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float BraceStiffen = 3.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlowElbowBrace = 45.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float WingsuitArms = 24.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float WingsuitLegs = 7.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float WingsuitForce = 2.5f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float ShoulderDip = 22.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float ShoulderDipMax = 28.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float TorsoArch = 8.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float RudderPerForce = 10.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float RudderPerTurn = 8.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float RudderMax = 18.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float ElevatorPerDegree = 0.35f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float ElevatorMax = 20.f;
    /** Deceleration that fully engages the authored braking pose, in cm/s squared. */
    UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "251"))
    float FlapBrake = 2500.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlapArms = 30.f;
    UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "RPG|Flight Animation|Legacy Steering", meta = (ToolTip = "Legacy asset compatibility only; inactive. Edit the authored flight clips instead."))
    float FlapLegs = 8.f;
	/** The body turns after the view instead of snapping with it (higher = quicker to catch up). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
	float TurnFollowSpeed = 5.f;

	/** Legacy setting retained for existing assets. Physics follow-through stays disabled; use bLimbFlow. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction")
	bool bFollowThrough = false;   // off: physics follow-through jittered (2026-10-23); an animation-side follow-through replaces it
	/** The body turns about this height above the feet (the hips). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction")
	float PivotHeight = 95.f;

private:
	/** Body reaction: turns the mesh about the hips; restores it when flight ends. */
	void UpdateBodyReaction(class USkeletalMeshComponent* Mesh, const FVector& Velocity, float DeltaTime);
	void ResetBodyReaction(class USkeletalMeshComponent* Mesh, float DeltaTime = 0.f, bool bGrounded = false, float PoseReleaseAlpha = -1.f);
	void UpdateLimbFlow(class USkeletalMeshComponent* Mesh, class UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime);
    void ResolveFlightClips(class UAnimInstance* Instance);
    UPROPERTY(Transient)
    TArray<TObjectPtr<UAnimSequence>> ResolvedClips;
    UPROPERTY(Transient)
    TObjectPtr<class USkeleton> ResolvedSkeleton;
    void StartFlightAction(int32 ClipIndex);
    int32 ActionIndex = INDEX_NONE;
    float ActionTime = 0.f;
    float ReleaseTravel = 1.f;
    bool bReleaseReentry = false;
    float ReleaseReentryBlend = 1.f;
    bool bAccelerationArmed = true, bBoostArmed = true;
    float TurnPose = 0.f;
    float Phase = 0.f;
    float BrakePoseWeight = 0.f;
    float StopBraceHold = 0.f;
    bool bWasFlying = false, bVelocityInitialized = false;
    FVector FilteredAcceleration = FVector::ZeroVector;
    float FilteredTurnRate = 0.f;
	TArray<FVector> FlowAngle, FlowSpeed, FlowDir;
    FVector2D RearAngle = FVector2D::ZeroVector, RearVelocity = FVector2D::ZeroVector;
    float RearKneeAngle = 0.f, RearKneeVelocity = 0.f;
	float SideForce = 0.f;
	float FlowWeight = 0.f;
	FVector FlowLastVelocity = FVector::ZeroVector;
	bool bReacting = false;
    bool bTravelHeadingValid = false;
    bool bVerticalFacingActive = false;
	FVector RestLocation = FVector::ZeroVector;
	FQuat RestRotation = FQuat::Identity;
	float LastHeading = 0.f, Pitch = 0.f, Roll = 0.f;
	float SmoothedYaw = 0.f, TurnRateRad = 0.f;

};
