#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGBodyImpactComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimSequence;
class UPrimitiveComponent;
class URPGPhysicsReactionComponent;

DECLARE_MULTICAST_DELEGATE(FRPGOnKnockedDown);

/**
 * Body-to-body collisions for any humanoid (player and NPCs), like RDR2 / GTA: when this character's capsule runs into
 * another character fast enough, momentum decides what happens. The struck one is thrown into a whole-body ragdoll with
 * its share of the momentum (both bodies' realistic weights, RPGBodyMass, and a little bounce); if the impact is hard
 * enough for the one moving, it goes down too. Bodies then lie where they land and get up there (a get-up chosen by how
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

	/** Closing speed (cm/s) at which the character run into is knocked down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Body Impact", meta = (ClampMin = "0"))
	float KnockDownSpeed = 450.f;
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

	/** Throw this character into a whole-body ragdoll moving at Velocity (world, cm/s), struck at Point. */
	void KnockDown(const FVector& Velocity, const FVector& Point);
	bool IsDown() const { return bDown; }
	/** Fired when this character is knocked down by an impact (NPCs stop their AI / step reactions). */
	FRPGOnKnockedDown OnKnockedDown;

	/** Stand Character up where its ragdoll lies: picks one of GASP's 12 get-ups by how the body lies (face down, back,
	 *  left or right side) and intent (bWantsToMove: up into a run; otherwise a mix of stand, roll and crouch scramble),
	 *  turns and places the capsule so the clip's first frame lies on the body, plays it at Rate.
	 *  Returns the montage (null if none could play) and its duration. Shared by NPC knockdowns and the player. */
	static UAnimMontage* StandUp(ACharacter* Character, float Rate, float& OutDuration, bool bWantsToMove = false);

private:
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	void HandleLimpEnding();
	URPGPhysicsReactionComponent* GetReaction(bool bCreate);

	FVector LastVelocity = FVector::ZeroVector;
	bool bDown = false;
	bool bOwnLimpHandler = false;
	float GettingUpUntil = 0.f;
	float LastImpactTime = -10.f;
};
