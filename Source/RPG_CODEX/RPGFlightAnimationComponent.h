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
	bool bReacting = false;
	FVector RestLocation = FVector::ZeroVector;
	FQuat RestRotation = FQuat::Identity;
	FVector LastVelocity = FVector::ZeroVector;
	float LastHeading = 0.f, Pitch = 0.f, Roll = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Playing;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;
};
