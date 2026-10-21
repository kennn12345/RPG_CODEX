#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGCombatAimComponent.generated.h"

class ACharacter;
class URPGCombatStateComponent;

/**
 * Look-directed attacks, shared by first and third person. The camera / reticle is the aiming reference: a trace from
 * the camera through the screen centre finds the AimPoint (the part of the target under the reticle), and the body
 * turns underneath the camera so the current punches land there. No buttons or directional clips are involved, and
 * the camera is never moved to make the body reach.
 *
 * Aim angles are measured from the body's natural strike line (the punches' own impact point, from the spine in third
 * person and from the eyes in first person) to the AimPoint, then clamped to MaxTorsoYaw / MaxAimPitchUp / Down: the
 * reticle can look anywhere, the attack goes as far toward it as a body believably can (bAttackAimInReach says whether
 * that is all the way).
 *
 * While fighting (combat stance, attacks, guard):
 *  - the torso turns toward the aim (URPGAnimInstance): yaw up the spine all the time, pitch while an attack is under
 *    way (holding the stance with a tilted chest would look odd); in first person the pitch tilts the chest about the
 *    eyes, like a view model;
 *  - standing, the body takes over what the torso can't: past MaxTorsoYaw it turns (at most BodyTurnSpeed), and it
 *    slowly squares up to the aim; kicks turn the body with only a little torso turn;
 *  - moving, the legs keep the locomotion's facing (third person: toward the movement) and only the torso aims.
 * Outside combat, third person moves as before and first person faces the camera as before.
 *
 * Publishes AttackAimPoint / AttackAimDirection / AttackAimYaw / AttackAimPitch on the combat state.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGCombatAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGCombatAimComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Furthest the torso turns left / right of the hips (degrees). Beyond it the body turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0", ClampMax = "70"))
	float MaxTorsoYaw = 40.f;

	/** Furthest the strike tilts up / down from its natural line (degrees). The reticle may look further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0", ClampMax = "45"))
	float MaxAimPitchUp = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0", ClampMax = "45"))
	float MaxAimPitchDown = 30.f;

	/** Targets under the reticle count up to this far from the character (cm); beyond, the aim uses only the look direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "50"))
	float MeleeAimRange = 300.f;

	/** Mesh space: where the strike turns from in third person (spine_03 in the stance), and where the punches land
	 *  with no aim (their impact point, between the two hands). Used to measure the aim from the strike's own line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim")
	FVector StrikePivot = FVector(-1.7f, -0.1f, 108.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim")
	FVector NaturalStrikePoint = FVector(-5.f, 107.f, 131.f);

	/**
	 * Third person: how far the fist sits from the centre of the arc the spine's pitch swings it on (cm). Measured in
	 * game: each degree of pitch moves the landing point about 1 cm up / down. The strike pitch is then the angle that
	 * lifts / lowers the landing point to the reticle's height. (First person tilts rigidly about the eyes, so its
	 * angle comes straight from the eyes.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "10"))
	float ThirdPersonStrikeRadius = 57.f;

	/** Fastest the body turns when the aim is past the torso's reach (degrees / second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float BodyTurnSpeed = 240.f;

	/** Standing in combat, the share of the remaining torso twist the body squares up per second. 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float SquareUpRate = 1.2f;

	/** During kicks the torso keeps only this share of its turn; the body turns for the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0", ClampMax = "1"))
	float KickTorsoShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float KickTurnSpeed = 360.f;

	/** How fast the aim fades in on entering combat and out on leaving it (full weight per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0.1"))
	float AimBlendSpeed = 7.f;

	/** How fast the strike pitch comes in when an attack starts and leaves after it (full weight per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0.1"))
	float AttackPitchBlendSpeed = 8.f;

	/**
	 * How quickly the body's visible aim catches up with the raw aim (per second). The camera / reticle always respond
	 * at once; only the body follows. First person: the arms are seen right against the view, so they track it closely
	 * (a slower follow makes every small stick move swim the hands across the screen). Third person: the body follows
	 * calmly. During the strike itself both use StrikeAimSmoothing, so the fist arrives where the reticle is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float FirstPersonAimSmoothing = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float ThirdPersonAimSmoothing = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Aim", meta = (ClampMin = "0"))
	float StrikeAimSmoothing = 30.f;

	/** 0 = no aim (exploring), 1 = fully aiming (fighting). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	float AimAlpha = 0.f;

	/** 0..1: how much of the strike pitch is applied (an attack is under way). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Aim")
	float AttackPitchAlpha = 0.f;

private:
	URPGCombatStateComponent* GetCombatState() const;
	/** The camera's view (location / rotation): the player's camera, or the controller's eyes without one. */
	void GetView(const ACharacter* Character, FVector& OutLocation, FRotator& OutRotation) const;
	/** The point the reticle is on: the first thing under it within MeleeAimRange, else a point along the view. */
	FVector FindAimPoint(const ACharacter* Character, const FVector& ViewLocation, const FVector& ViewDirection, bool bFirstPerson, bool& bOutOnTarget) const;

	float SmoothedYaw = 0.f;
	float SmoothedPitch = 0.f;
};
