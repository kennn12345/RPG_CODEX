#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGHitReactionComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Ordinary-hit body reactions, physics-calculated but applied on the animation (no simulated bodies, so nothing can
 * jitter or flail). A hit at a point, in a direction, with a force (plus the attacker's momentum) twists each joint
 * between the hit point and the hips by the torque it puts there over the weight it has to move (the realistic
 * body-part weights, RPGBodyMass); each joint then swings on a damped spring and settles back. The arms trail the
 * torso's jolt a moment later. Knockdowns and knockouts still go to the real ragdoll (URPGPhysicsReactionComponent).
 *
 * The result is written to the character's URPGAnimInstance (FlowBones / FlowRotations), which turns those bones on
 * top of the animation.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGHitReactionComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** A hit on Bone at Point (world), pushing with Impulse (world, kg cm/s). */
	void AddHit(FName Bone, const FVector& Point, const FVector& Impulse);

	/** How strongly a given torque turns a joint (1 = the plain physics estimate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Reaction", meta = (ClampMin = "0"))
	float Responsiveness = 1.f;
	/** Pull back to the pose (higher = quicker, firmer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Reaction", meta = (ClampMin = "1"))
	float Stiffness = 70.f;
	/** 1 = settles without overshoot; lower = a little sway back past the pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Reaction", meta = (ClampMin = "0.1", ClampMax = "2"))
	float Damping = 0.8f;
	/** How much the arms trail the torso's jolt (fraction of the chest's swing, the other way, a moment later). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Reaction", meta = (ClampMin = "0", ClampMax = "1"))
	float ArmTrail = 0.45f;
	/** Overall cap on any joint's swing (degrees); each joint also has its own anatomical limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Reaction", meta = (ClampMin = "0"))
	float MaxAngle = 35.f;

private:
	struct FJoint
	{
		FName Bone;
		int32 Parent = INDEX_NONE;   // index in Joints of the nearest joint above
		float Limit = 0.f;           // radians
		float Stiff = 1.f;
		float MovedMass = 1.f;       // kg of everything this joint turns (the bodies below it in the skeleton)
		bool bArm = false;
		FVector Angle = FVector::ZeroVector;   // component space rotation vector
		FVector Speed = FVector::ZeroVector;
	};
	void SetUpJoints(USkeletalMeshComponent* Mesh);
	int32 FindJoint(FName Bone, const USkeletalMeshComponent* Mesh) const;
	USkeletalMeshComponent* GetMesh() const;

	TArray<FJoint> Joints;
	bool bActive = false;
};
