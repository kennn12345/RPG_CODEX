#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "RPGReactivePhysicsComponent.generated.h"

class USkeletalMeshComponent;
class URPGPhysicsReactionComponent;
class UAnimInstance;

UENUM(BlueprintType)
enum class ERPGReactivePhysicsState : uint8 { Animated, Reacting, Recovering, Ragdoll };

/** Physical Animation settings owned by the component; no shared Physics Asset edits are required. */
USTRUCT(BlueprintType)
struct FRPGReactivePhysicsProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics") float OrientationStrength = 650.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics") float AngularDamping = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics") float MaxAngularForce = 60000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics", meta=(ClampMin="0",ClampMax="1")) float HitMuscleScale = .45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics", meta=(ClampMin="0",ClampMax="1")) float MaxBlend = .65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics", meta=(ClampMin="0.01")) float BlendInSeconds = .08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics", meta=(ClampMin="0")) float HoldSeconds = .16f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reactive Physics", meta=(ClampMin="0.05")) float RecoverySeconds = .65f;
};

/** Phase one: bounded local Chaos reactions driven toward the live animation, feet/pelvis anchored.
 * Full ragdoll and pose-matched get-ups are handed to the existing RPGPhysicsReaction/BodyImpact system.
 * Balance/stepping are deliberately separate from this component's physical-animation ownership. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGReactivePhysicsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGReactivePhysicsComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	/** World-space impulse (kg cm/s), position and requested bone. False means untouched: caller may use its legacy fallback. */
	UFUNCTION(BlueprintCallable, Category="RPG|Reactive Physics")
	bool ApplyHit(FName HitBone, FVector ImpactPoint, FVector Impulse, float Intensity = 1.f, bool bForceRagdoll = false);
	UFUNCTION(BlueprintCallable, Category="RPG|Reactive Physics") void Recover();
	/** Synchronous release before a legacy controller starts; preserves live body transforms/velocities. */
	void YieldToLegacy();
	UFUNCTION(BlueprintPure, Category="RPG|Reactive Physics") bool IsReady() const { return bReady; }
	UFUNCTION(BlueprintPure, Category="RPG|Reactive Physics") bool IsLocalReactionActive() const { return bOwnsBodies; }
	UFUNCTION(BlueprintPure, Category="RPG|Reactive Physics") float GetBodyBlend(FName Bone) const;
	UFUNCTION(BlueprintPure, Category="RPG|Reactive Physics") int32 GetSimulatingUpperBodies() const;
	const TArray<FName>& GetUpperBodies() const { return UpperBodies; }
	UPhysicalAnimationComponent* GetPhysicalAnimation() const { return PhysicalAnimation; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") bool bEnabled = true;
	/** Explicit rig configuration, validated against the actual mesh/Physics Asset at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") FName UpperBodyRoot = TEXT("spine_02");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") FRPGReactivePhysicsProfile Profile;
	/** Optional authored Physics Asset profile. Missing per-body profiles retain the inline defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") FName PhysicalAnimationProfile;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics", meta=(ClampMin="0")) float ImpulseScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics", meta=(ClampMin="1")) float MaxLocalVelocityChange = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics", meta=(ClampMin="1")) float RagdollVelocityThreshold = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") bool bAutomaticRagdoll = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics", meta=(ClampMin="0.1")) float RagdollSeconds = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics", meta=(ClampMin="0",ClampMax="1")) float NeighborBlend = .32f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Reactive Physics") bool bDrawDebug = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="RPG|Reactive Physics") ERPGReactivePhysicsState State = ERPGReactivePhysicsState::Animated;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="RPG|Reactive Physics") FName LastHitBone;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="RPG|Reactive Physics") int32 AppliedImpulses = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="RPG|Reactive Physics") float PeakBlend = 0.f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="RPG|Reactive Physics") float LastAppliedImpulse = 0.f;

private:
	USkeletalMeshComponent* ResolveMesh() const;
	void TickReaction(float Dt);
	FName ResolveBody(FName Bone) const;
	bool AcquireBodies();
	void ReleaseBodies(bool bRestoreAnimation);
	void SetBlend(FName Bone, float Blend);
	void ApplyMuscles();
	void DrawReaction() const;
	void EnsurePoseEvaluation();
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(Transient) TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;
	UPROPERTY(Transient) TObjectPtr<URPGPhysicsReactionComponent> Legacy;
	TArray<FName> UpperBodies;
	TMap<FName,float> BodyBlends, TargetBlends;
	struct FPendingHit { FName Bone; FVector Point, Impulse; int32 Frames = 0; };
	TArray<FPendingHit> PendingHits;
	FVector LastPoint = FVector::ZeroVector, LastImpulse = FVector::ZeroVector;
	float ReactionTime = 0.f, RecoveryTime = 0.f, ReactionIntensity = 1.f;
	bool bReady = false, bOwnsBodies = false;
	ECollisionEnabled::Type SavedCollision = ECollisionEnabled::NoCollision;
	EVisibilityBasedAnimTickOption SavedTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	bool bSavedRateOptimizations = false;
	bool bReportedEmptyPose = false;
	TWeakObjectPtr<UAnimInstance> SynchronousAnim;
	bool bSavedParallelUpdate = false;
	bool bConfiguredPose = false;
};
