#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPowerTypes.h"
#include "RPGBodyImpactComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimSequence;
class UPrimitiveComponent;
class URPGPhysicsReactionComponent;

DECLARE_MULTICAST_DELEGATE(FRPGOnKnockedDown);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRPGOnBumped, const FVector& /*Velocity*/, const FVector& /*Point*/, ERPGPowerReaction /*Reaction*/);

/**
 * Body-to-body collisions for any humanoid (player and NPCs), like RDR2 / GTA: when this character's capsule runs into
 * another character, momentum decides what happens. The struck one takes its share of the momentum (both bodies'
 * realistic weights, RPGBodyMass, and a little bounce) and the Power System classifies the bump by closing speed: walking
 * into someone nudges them, running staggers them, sprinting knocks them back a few steps. Only a crash well past a
 * sprint (flying into them) throws them into a whole-body ragdoll; if it is hard enough for the one moving, it goes
 * down too. Bodies then lie where they land and get up there (a get-up chosen by how
 * the body lies, the same as after a knockdown).
 *
 * While the body is down the capsule follows it, so the camera (for the player) stays with the body.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGBodyImpactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGBodyImpactComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Bumps slower than this (cm/s) do nothing at all. What faster ones do (nudge, stagger, knockback, or a knockdown
	 *  only for a flying crash) is the Power System's call: Project Settings > RPG Power System > Body Bumps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0"))
	float MinBumpSpeed = 60.f;
	/** Closing speed at which this character goes down too (a hard crash into someone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0"))
	float SelfKnockDownSpeed = 1100.f;
	/** Bounce of a body-to-body impact (0 = the bodies move on together, 1 = perfectly elastic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0", ClampMax = "1"))
	float Restitution = 0.3f;
	/** Upward share of a knock (bodies hit at speed leave the ground a little). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0", ClampMax = "1"))
	float Lift = 0.15f;
	/** Seconds a knocked-down body stays down after it settles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0"))
	float DownTime = 1.5f;
	/** Total body weight (lb) for the momentum split; 0 = the project's Humanoid Body Weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0"))
	float BodyWeightLbs = 0.f;

	/** Throw this character into a whole-body ragdoll moving at Velocity (world, cm/s), struck at Point. It fights the
	 *  fall (URPGHitOutcomeComponent::BeginFall) unless bFallPrepared says the caller already set that up. */
	void KnockDown(const FVector& Velocity, const FVector& Point, bool bFallPrepared = false);
	bool IsDown() const { return bDown; }
	/** Fired when this character is knocked down by an impact (NPCs stop their AI / step reactions). */
	FRPGOnKnockedDown OnKnockedDown;
	/** Run into without going down: its share of the momentum (world, cm/s), where it was touched, and how hard it
	 *  felt (Flinch = a nudge, Stagger, Knockback). NPCs react on their own; anyone else just takes the push. */
	FRPGOnBumped OnBumped;
	/** Take a bump that doesn't knock this character down (see OnBumped). */
	void Bump(const FVector& Velocity, const FVector& Point, ERPGPowerReaction Reaction);
	/** How the last bump this character took was classified (tests and the debug readout). */
	ERPGPowerReaction GetLastBumpReaction() const { return LastBumpReaction; }
	int32 GetBumpCount() const { return BumpCount; }
	/** Per-character history, only used to break ties between safe, close pose matches. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="RPG|Get Up")
	FName LastGetUpClip;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="RPG|Get Up")
	FName LastGetUpStyle;
	bool IsGetUpAligning() const { return bGetUpAwaitingPhysics; }

	/** Stand Character up where its ragdoll lies: picks between the two mocap get-ups by pose and intent,
	 *  using one best start frame per clip and avoiding repeated recovery styles among safe near-equal matches,
	 *  turns and places the capsule so the clip's first frame lies on the body, plays it at Rate.
	 *  Returns the montage (null if none could play) and its duration. Shared by NPC knockdowns and the player. */
	static UAnimMontage* StandUp(ACharacter* Character, float Rate, float& OutDuration, bool bWantsToMove = false);

private:
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	void HandleLimpEnding();
	URPGPhysicsReactionComponent* GetReaction(bool bCreate);
	/** While the player is down the capsule follows the body (for the camera). The ragdoll's mesh is placed absolutely
	 *  meanwhile, so those moves don't reach it (each one was an engine warning: "moving a fully simulated mesh"), and
	 *  goes back under the capsule at its usual offset when it gets up. */
	void FreeMeshFromCapsule(ACharacter* Character);
	void ReturnMeshToCapsule(ACharacter* Character);
	bool bMeshFreed = false;
	FVector MeshOffset = FVector::ZeroVector;
	FRotator MeshTurn = FRotator::ZeroRotator;

	FVector LastVelocity = FVector::ZeroVector;
	bool bDown = false;
	bool bOwnLimpHandler = false;
	float GettingUpUntil = 0.f;
	float LastImpactTime = -10.f;
	ERPGPowerReaction LastBumpReaction = ERPGPowerReaction::Flinch;
	int32 BumpCount = 0;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> HeldGetUp;
	float GetUpTargetRate = 1.f;
	float GetUpResumeTime = 0.f;
	bool bGetUpAwaitingPhysics = false;
};
