#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGFlightAnimationComponent.generated.h"

class UAnimMontage;
class UAnimSequence;

/**
 * Flight poses: while the character is flying, loops hover / forward / fast flight (RPG_Proto_Fly*) on the full body,
 * picked by flight speed (fast flight on L3 always uses the fast pose), cross-fading when it changes. An attack montage
 * takes over the slot while it plays; the flight loop comes back on its own afterwards. Stops when flight ends.
 * The whole body also reacts to the flight (turning the mesh about the hips): pitches up climbing and down diving, leans
 * into speeding up and back when slowing, banks into turns - a little at a hover, fully at speed.
 * Not yet: upper-body-only attacks in the air.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGFlightAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGFlightAnimationComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Hover;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Forward;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation")
	TSoftObjectPtr<UAnimSequence> Fast;

	/** Horizontal flight speed (cm/s) above which the forward pose plays instead of hover... */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float ForwardSpeed = 150.f;

	/** ...and above which the fast pose plays. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float FastSpeed = 900.f;

	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation", meta = (ClampMin = "0"))
	float BlendTime = 0.35f;

	/** Climb / dive: the body pitches by this fraction of the flight path's angle (up to MaxPitch). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float ClimbPitch = 1.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float MaxPitch = 50.f;
	/** Degrees of lean per 1000 cm/s^2 of acceleration: forward when speeding up, back when slowing, sideways in turns. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float AccelLean = 10.f;
	/** Bank into turns: degrees per degree-per-second of turning, at FastSpeed (less when slower), up to MaxBank. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float BankPerTurn = 0.6f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0"))
	float MaxBank = 55.f;
	/** How much of all this at a hover (1 at FastSpeed and above). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0", ClampMax = "1"))
	float HoverReaction = 0.5f;
	/** How quickly the body follows (higher = snappier). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
	float ReactionSpeed = 6.f;
	/** Follow-through while flying: arms, legs and lower back are springs pulled back to the animated pose. Momentum
	 *  swings them off it (they lag when speeding up, swing on when stopping, swing out in turns), drag sweeps them back
	 *  at speed, and the pull back weakens the faster you fly. Head and upper chest are not moved. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through")
	bool bLimbFlow = true;
	/** Pull back to the pose (higher = stiffer, quicker to return). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "1"))
	float FlowStiffness = 60.f;
	/** At FastSpeed the pull back is this fraction of FlowStiffness (harder to hold the pose at speed). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "1"))
	float FlowStiffnessAtSpeed = 0.6f;
	/** Bounce: lower = more overshoot and swinging back and forth, 1 = settles without overshoot. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0.05", ClampMax = "2"))
	float FlowDamping = 0.35f;
	/** How hard momentum swings the limbs (per 1000 cm/s^2 of speeding up, slowing or turning). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowInertia = 16.f;
	/** How hard the air sweeps them back (per 1000 cm/s, growing with speed). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowDrag = 2.5f;
	/** Largest swing of any joint, degrees. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowMaxAngle = 28.f;
	/** Turning swings the limbs too (they lag behind the body's turn). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowTurn = 0.3f;
	/** Turning at speed: the outer arm braces out from the body (turning left, the right arm), driven by the turn's
	 *  sideways force (speed x how sharply you turn): this many degrees per 1000 cm/s^2 of it, up to FlowBraceMax. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowBrace = 22.f;
	/** Extra bracing that grows with the square of the sideways force: hard, fast corners brace far more. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowBraceSharp = 12.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float FlowBraceMax = 80.f;
	/** A braced arm stiffens: at full brace its pull to the brace is this many times stronger (1 + this), and the forearm
	 *  is held straight, so it barely wobbles. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Follow Through", meta = (ClampMin = "0"))
	float BraceStiffen = 3.f;
	/** Wingsuit: in fast, hard corners both arms and both legs spread out from the body (degrees at full spread), by the
	 *  turn's sideways force times speed: full spread at WingsuitForce (1000 cm/s^2 units) at FastSpeed, fading as you slow. */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float WingsuitArms = 55.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float WingsuitLegs = 18.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0.1"))
	float WingsuitForce = 2.5f;
	/** Steering, like a plane's control surfaces (degrees at full effect): the legs swing together to the outside of a
	 *  turn (rudder) per radian-per-second of turning; climbing swings them forward and under, diving lets them trail
	 *  back (elevators) per degree of flight-path angle; braking hard spreads the arms and opens the legs (flaps). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float RudderPerTurn = 18.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float RudderMax = 30.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float ElevatorPerDegree = 0.35f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float ElevatorMax = 20.f;
	/** Full flaps at this much slowing down (cm/s^2). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "1"))
	float FlapBrake = 2500.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float FlapArms = 30.f;
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Steering", meta = (ClampMin = "0"))
	float FlapLegs = 8.f;
	/** The body turns after the view instead of snapping with it (higher = quicker to catch up). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction", meta = (ClampMin = "0.1"))
	float TurnFollowSpeed = 5.f;

	/** Hips, legs and arms trail the chest and head on physics while flying (URPGPhysicsReactionComponent::SetFlightFollow). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction")
	bool bFollowThrough = false;   // off: physics follow-through jittered (2026-10-23); an animation-side follow-through replaces it
	/** The body turns about this height above the feet (the hips). */
	UPROPERTY(EditAnywhere, Category = "RPG|Flight Animation|Body Reaction")
	float PivotHeight = 95.f;

private:
	/** Body reaction: turns the mesh about the hips; restores it when flight ends. */
	void UpdateBodyReaction(class USkeletalMeshComponent* Mesh, const FVector& Velocity, float DeltaTime);
	void ResetBodyReaction(class USkeletalMeshComponent* Mesh);
	void UpdateLimbFlow(class USkeletalMeshComponent* Mesh, class UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime);
	TArray<FVector> FlowAngle, FlowSpeed, FlowDir;
	float SideForce = 0.f;
	float FlowWeight = 0.f;
	FVector FlowLastVelocity = FVector::ZeroVector;
	bool bReacting = false;
	FVector RestLocation = FVector::ZeroVector;
	FQuat RestRotation = FQuat::Identity;
	FVector LastVelocity = FVector::ZeroVector;
	float LastHeading = 0.f, Pitch = 0.f, Roll = 0.f;
	float SmoothedYaw = 0.f, TurnRateRad = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Playing;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;
};
