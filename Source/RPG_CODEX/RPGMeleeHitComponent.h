#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGMeleeHitComponent.generated.h"

class URPGCombatStateComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnMeleeHitSignature, const FRPGMeleeHitInfo&, Hit);

/**
 * Prototype unarmed hit detection. While the combat state's strike hit window is open (only around the impact of a
 * punch / kick), spheres on the striking fist or foot are swept from where they were last frame to where they are now,
 * so the check follows the actual limb in the animation. Every target is hit at most once per strike.
 *
 * Detects skeletal / physics bodies (Pawn and PhysicsBody object types), so a mannequin's physics asset gives
 * per-limb hits. It only reports contact (OnMeleeHit, IRPGMeleeTarget, a debug message); no damage or forces.
 *
 * Debug drawing: bDrawDebugTraces here, or the console variable rpg.MeleeDebug (0 = off, 1 = on).
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGMeleeHitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGMeleeHitComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Sphere radius on the fist (knuckles and the middle of the fist). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Melee", meta = (ClampMin = "0.5"))
	float FistRadius = 6.f;

	/** Sphere radius on the foot (ankle / heel and ball of the foot, covering the sole). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Melee", meta = (ClampMin = "0.5"))
	float FootRadius = 8.f;

	/** Draw the sweeps (limb colour while the window is open), hit points and labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Melee|Debug")
	bool bDrawDebugTraces = true;

	/** Seconds the debug sweeps and hits stay on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Melee|Debug", meta = (ClampMin = "0"))
	float DebugDrawTime = 2.f;

	/** Print "HIT: Right Punch ..." on screen and in the log (LogRPGInput). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Melee|Debug")
	bool bPrintHits = true;

	/** Fires once per target per strike. The Power System can bind here later. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Melee")
	FRPGOnMeleeHitSignature OnMeleeHit;

private:
	/** World positions of the striking limb's sweep spheres this frame (2 per limb). */
	bool GetLimbPoints(ERPGStrikeLimb Limb, TArray<FVector>& OutPoints) const;
	/** Travel: this frame's movement of the limb, used as the strike direction (for kicks the ankle's, i.e. the leg's,
	 *  not the ball of the foot, which swings up as the foot turns sole-forward at contact). */
	void ReportHit(const FHitResult& Hit, const FVector& End, const FVector& Travel, URPGCombatStateComponent* State);
	bool ShouldDrawDebug() const;
	USkeletalMeshComponent* GetMesh() const;

	/** Last frame's limb points, so the sweep covers the whole path between frames (fast strikes move 15-20 cm). */
	TArray<FVector> LastPoints;
	/** The strike contact's offset on the limb last frame: travel is measured on the animation (contact taken out). */
	FVector LastCorrection = FVector::ZeroVector;
	ERPGStrikeLimb LastLimb = ERPGStrikeLimb::None;
	int32 CurrentStrikeId = -1;
	/** Actors already hit by the current strike. */
	TArray<TWeakObjectPtr<AActor>> HitThisStrike;
};
