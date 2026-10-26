#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGPowerTypes.h"
#include "RPGEnergySpellTypes.h"
#include "RPGPhysicsReactionComponent.generated.h"

class UPhysicsControlComponent;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class ERPGPhysicsReactionState : uint8
{
	/** Animation only: every body is kinematic and the pose is the animation's. */
	Animated,
	/** Partial physics: the bodies simulate, motors pull each one back toward the animation, the pelvis stays on the
	 *  animation, and the struck limb's motors are weakened so it visibly gives way. */
	Powered,
	/** Full ragdoll: every body simulates and the motors are off. */
	Limp
};

/** What the last reaction did (debug output and the automation test read it). */
USTRUCT(BlueprintType)
struct FRPGPhysicsReactionInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	ERPGPhysicsReactionState State = ERPGPhysicsReactionState::Animated;

	/** The body that was hit (FRPGMeleeHitInfo::HitBone) and the limb it belongs to (ArmLeft, Spine, Head...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FName HitBone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FName Limb;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FVector ImpulseDirection = FVector::ZeroVector;

	/** kg cm/s, as applied to the body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float ImpulseMagnitude = 0.f;

	/** Set once the impulse has actually reached the simulated body (it waits for the body to start simulating). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	bool bImpulseApplied = false;

	/** Furthest the struck limb got from its animated position during this reaction, cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float PeakDeviation = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	FString AttackName;

	/** GetStrikeStrength of the hit: 1 = a typical tap at its usual speed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG")
	float Strength = 0.f;
};

/**
 * Physical hit reactions on a skeletal mesh, through Unreal's PhysicsControl plugin: a UPhysicsControlComponent is made
 * at BeginPlay with one body modifier per physics body and parent-space motors (each body driven toward its animated
 * pose relative to its parent), built from the mesh's own physics asset. No Physics Control Asset is needed.
 *
 * It does no hit detection: the owner passes on the melee hits it already receives (HandleMeleeHit), e.g. from
 * IRPGMeleeTarget::ReceiveMeleeHit. Written for any skeletal mesh actor (test dummy now; NPCs / player later), but it
 * leaves the mesh's animation alone: in ANIMATED the physics has zero blend weight.
 *
 * Prototype states: ANIMATED -> POWERED (partial physics, struck limb yields, motors recover) -> back to ANIMATED;
 * or LIMP (full ragdoll) -> back to ANIMATED by fading the pose to the animation. No get-up animations yet.
 *
 * Debug: bDrawDebug, or the console variable rpg.PhysicsReactionDebug (-1 = with rpg.Debug, 0 = off, 1 = on).
 * Every reaction is also logged (LogRPGInput).
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGPhysicsReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGPhysicsReactionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Legacy entry point (no Power System): full charge = LIMP, otherwise POWERED, with the TEMPORARY impulse values. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void HandleMeleeHit(const FRPGMeleeHitInfo& Hit);

	/**
	 * Carries out a Power System result. Flinch / Stagger: POWERED, the struck limb yields (Stagger harder).
	 * Knockdown: LIMP, collapsing with any shove velocity. Launch: LIMP with every body thrown at the result's
	 * DisplacementVelocity, staying limp for the flight plus LimpTime.
	 */
	void HandlePowerHit(const FRPGMeleeHitInfo& Hit, const FRPGPowerResult& Result);
    /** Local energy flinch / stagger. Leg hits yield their support while the owner takes a recovery step. */
    void HandleEnergyHit(const FRPGEnergyImpact& Impact);

	/** Fires once when a LIMP reaction starts fading back to the animation, while the pose is still the ragdoll's. The
	 *  owner can move itself to where the body ended up first (the dummy stands up where it landed). */
	FSimpleMulticastDelegate OnLimpEnding;

	/** Partial physics: bodies simulate under motors toward the animation, the limb holding HitBone yields. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void EnterPoweredReaction(FName HitBone, bool bDazed = false);

	/** Full ragdoll for LimpTime seconds (<= 0: until ReturnToAnimated is called), then returns on its own. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void EnterFullRagdoll(float LimpTime = -1.f);

	/** Impulse (kg cm/s) at a world point on HitBone's body. Waits for the body to be simulating if it is not yet. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void ApplyReactionImpulse(FName HitBone, FVector ImpactPoint, FVector Direction, float Magnitude);

	/** Fades the pose back to the animation over BlendTime (< 0: ReturnBlendTime), then makes every body kinematic. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void ReturnToAnimated(float BlendTime = -1.f);

	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	ERPGPhysicsReactionState GetReactionState() const { return State; }

	/** True while fading back to the animation. */
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	bool IsReturning() const { return bReturning; }

	/** Bodies currently simulating. */
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	int32 GetNumSimulatingBodies() const;

	/** The limb a bone belongs to (walks up to the nearest bone with a body). None if the mesh has no such bone. */
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	FName GetLimbForBone(FName Bone) const;

	/** How far the struck limb (its furthest bone) is off its animated position right now, cm. 0 when ANIMATED. */
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	float GetStruckChainDeviation() const;

	/** Debug: a powered hit on the chest from the front / a full ragdoll (Details panel buttons while playing). */
	/** Flight follow-through ("ribbon"): the chest, neck and head stay on the animation and lead; everything else (hips,
	 *  legs, arms) is physical without gravity, carried by its own momentum and pulled loosely back toward the pose by
	 *  motors at FlightMotorScale, so it trails, swings out and catches up when the leader turns, climbs or dives. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Physics Reaction")
	void SetFlightFollow(bool bOn);

	/** Whole-body ragdoll moving at Velocity (world cm/s; body parts near Point a little more), limp for LimpSeconds
	 *  after it settles. Collisions, throws, explosions. */
	void Launch(const FVector& Velocity, const FVector& Point, float LimpSeconds, bool bNaturalFall = false);
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	bool IsFlightFollowing() const { return bFlightFollow; }

	UFUNCTION(CallInEditor, Category = "RPG|Physics Reaction|Debug")
	void DebugPoweredHit();

	UFUNCTION(CallInEditor, Category = "RPG|Physics Reaction|Debug")
	void DebugFullRagdoll();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction")
	bool bReactionsEnabled = true;

	// --- Motors (parent-space controls). Strengths are drive frequencies (acceleration mode, so mass independent). ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0"))
	float MotorStrength = 40.f;

	/** Ordinary hits (POWERED): the most the physics may take over the pose. Below 1 the animation never fully lets
	 *  go, so a hit is a give in the body rather than a floppy limb, however hard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0", ClampMax = "1"))
	float PoweredMaxBlend = 0.7f;

	/** Ordinary hits: keep the legs on the animation (a struck leg is shown by a balance step instead), so physics never
	 *  flings a leg about. The rest of the body still gives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered")
	bool bKinematicLegsWhenPowered = false;

	/** Rotation damping put on every ragdoll body (the physics asset's own is replaced). 0.01 is UE5 Manny's; GASP's
	 *  mannequin asset uses 5, which makes hits look locked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction", meta = (ClampMin = "0"))
	float BodyAngularDamping = 0.01f;

	/** Put the engine's default physical material on every ragdoll body instead of the physics asset's own (GASP's
	 *  makes the feet grip the floor so only the top half falls). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction")
	bool bUseDefaultPhysMaterial = true;

	/** Ordinary hits (flinch / stagger) move the whole body on motors without ragdolling: everything but the pelvis
	 *  takes the force and is pulled back to the animation, legs included (stiffer, and they don't collide with the
	 *  floor meanwhile so the feet can't be shoved apart). Off: ordinary hits leave the body on its animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered")
	bool bHitReactions = true;

	/** Flight follow-through: motor strength for the trailing parts (multiple of MotorStrength). High enough that the
	 *  body keeps its pose and shape; the lag on turns / climbs / dives comes from momentum. Lower = looser and floatier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Flight", meta = (ClampMin = "0"))
	float FlightMotorScale = 3.f;
	/** Extra stiffness for the hips and lower back in flight (the body's structure); the legs and arms flow more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Flight", meta = (ClampMin = "0"))
	float FlightSpineScale = 2.f;
	/** Bones that lead in flight (kinematic, with the head / neck limb). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Flight")
	TArray<FName> FlightLeaderBones = { TEXT("spine_04"), TEXT("spine_05") };

	/** Leg motors are this many times the others, so the legs give a little but keep it standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0"))
	float LegMotorScale = 2.5f;

    /** Energy hits only: the struck leg yields instead of retaining the usual extra standing stiffness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Stagger", meta = (ClampMin = "0.1"))
    float EnergyLegMotorScale = 0.9f;

	/** Stagger (dazed): every motor starts at this fraction of its strength and recovers over StaggerRecoverTime, so the
	 *  whole body wobbles; the struck part yields for StaggerYieldTime first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Stagger", meta = (ClampMin = "0", ClampMax = "1"))
	float StaggerMotorScale = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Stagger", meta = (ClampMin = "0"))
	float StaggerYieldTime = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Stagger", meta = (ClampMin = "0"))
	float StaggerRecoverTime = 1.6f;

	/** Knockdowns / launches: how much of the trunk's starting speed the arms and legs get (0 = they start still and
	 *  are only dragged along, 1 = the whole body moves as one). The body's total momentum is the same either way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0", ClampMax = "1"))
	float LimbVelocityShare = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0"))
	float MotorDampingRatio = 1.f;

	/** The struck limb's motors drop to this fraction of MotorStrength on impact... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0", ClampMax = "1"))
	float StruckLimbStrengthScale = 0.25f;

	/** ...stay there this long... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0"))
	float PoweredYieldTime = 0.1f;

	/** ...then ramp back to full strength over this long, after which the pose fades back to the animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered", meta = (ClampMin = "0"))
	float PoweredRecoverTime = 0.6f;

	/** Seconds to fade from the physics pose to the animation at the end of a reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction", meta = (ClampMin = "0"))
	float ReturnBlendTime = 0.35f;

	/** Bone that stays on the animation in POWERED, so the body keeps its footing (only the rest of it yields). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Powered")
	FName AnchorBone = TEXT("pelvis");

	// --- Limp ---

	/** Charged hits released at or above this charge go LIMP instead of POWERED (> 1 = never). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0"))
	float LimpChargeThreshold = 0.99f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0"))
	float LimpTime = 2.f;

	/** Power System ragdolls: after LimpTime the body only starts getting up once its pelvis is slower than this (cm/s),
	 *  so it doesn't stand up while still sliding or tumbling... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0"))
	float LimpSettleSpeed = 40.f;

	/** ...but never waits longer than this beyond LimpTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0"))
	float LimpMaxSettleWait = 1.5f;

	/** Fade from the ragdoll back to the animation (a placeholder until there are get-up animations). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Limp", meta = (ClampMin = "0"))
	float LimpReturnBlendTime = 0.5f;

	// --- Impulse. TEMPORARY placeholder values: the Power System will own the real magnitudes. ---

	/** TEMPORARY. Speed (cm/s) the impulse gives the struck body alone: impulse = this * the body's mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Impulse (TEMPORARY)", meta = (ClampMin = "0"))
	float PlaceholderImpulseSpeed = 350.f;

	/** TEMPORARY. Extra impulse at full charge (x(1 + this * ChargePercent)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Impulse (TEMPORARY)", meta = (ClampMin = "0"))
	float PlaceholderChargeScale = 1.f;

	/** Limb speed (cm/s, from the animation) of a typical tap strike: strength 1 at this speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Impulse (TEMPORARY)", meta = (ClampMin = "1"))
	float ReferenceStrikeSpeed = 800.f;

	/** How much the strike's speed scales the impulse (0 = not at all, 1 = in proportion), within 0.4x..1.6x. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Impulse (TEMPORARY)", meta = (ClampMin = "0", ClampMax = "1"))
	float StrikeSpeedInfluence = 0.6f;

	/** How hard a hit is, 1 = a typical tap: its limb speed against ReferenceStrikeSpeed (by StrikeSpeedInfluence) times
	 *  the charge bonus. Scales the impulse; the owner can use it for its own response (the dummy's shove). */
	UFUNCTION(BlueprintPure, Category = "RPG|Physics Reaction")
	float GetStrikeStrength(const FRPGMeleeHitInfo& Hit) const;

	// --- Power System reactions (HandlePowerHit) ---

	/** Speed (cm/s) a Flinch gives the struck body: impulse = this * the body's mass. Only how much the limb yields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Power", meta = (ClampMin = "0"))
	float FlinchImpulseSpeed = 120.f;

	/** Same for a Stagger (and the topple push of a Knockdown). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Power", meta = (ClampMin = "0"))
	float StaggerImpulseSpeed = 220.f;

	/** TEMPORARY. Impulse multiplier when the hit sends the body LIMP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Impulse (TEMPORARY)", meta = (ClampMin = "0"))
	float PlaceholderLimpImpulseScale = 2.f;

	// --- Debug ---

	/** Hit bone, impact point, impulse arrow and magnitude, current state above the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Debug")
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Physics Reaction|Debug", meta = (ClampMin = "0"))
	float DebugDrawTime = 2.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Physics Reaction|Debug")
	FRPGPhysicsReactionInfo LastReaction;

	/** Impulses that have reached a body since BeginPlay (tests check there is exactly one per hit). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Physics Reaction|Debug")
	int32 ImpulsesApplied = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Physics Reaction|Debug")
	TObjectPtr<UPhysicsControlComponent> PhysicsControl;

private:
	USkeletalMeshComponent* GetMesh() const;
	bool SetUpControls();
	void SetLimbMotorScale(FName Limb, float Scale);
	void SetAllMotorScale(float Scale);
	/** Powered motors at recovery R (0 = just hit, 1 = recovered): all, legs, struck limb. */
	void ApplyPoweredMotorScales(float R);
	void SetPhysicsBlend(float Weight);
	void FinishReturn();
	void ApplyPendingImpulse();
	void ApplyPendingVelocity();
	/** The body to push for a hit on Bone: it, or the nearest body above it (avoiding the anchor unless LIMP). */
	FName FindImpulseBody(FName Bone, bool bLimp) const;
	bool ShouldDrawDebug() const;

	ERPGPhysicsReactionState State = ERPGPhysicsReactionState::Animated;
	bool bSetUp = false;

	/** Body modifier per bone, the bone's limb, and each limb's motors. */
	TMap<FName, FName> BoneModifiers;
	TMap<FName, FName> BoneLimbs;
	TMap<FName, TArray<FName>> LimbControls;
	TArray<FName> AllModifiers;
	TArray<FName> LegModifiers;
	/** Ragdolls are solid: the body parts collide with the world, other characters, other ragdolls and each other, and
	 *  the owner's capsule stops blocking (its body has left it). Restored when the body is back on the animation. */
	void SetSolidRagdoll(bool bSolid);
	bool bSolidRagdoll = false;
	/** Launched by a body impact: real gravity and a slicker body on the ground, so it tumbles, rolls and slides out
	 *  the momentum it has instead of dropping and gripping. */
	bool bNaturalFall = false;
	UPROPERTY(Transient)
	TObjectPtr<class UPhysicalMaterial> TumbleMaterial;
	TEnumAsByte<ECollisionChannel> SavedMeshObjectType = ECC_Pawn;
	FCollisionResponseContainer SavedMeshResponses;
	TArray<TPair<TWeakObjectPtr<class UPrimitiveComponent>, FCollisionResponseContainer>> SavedCapsuleResponses;
	bool bFlightFollow = false;
	float CurrentYieldTime = 0.1f, CurrentRecoverTime = 0.6f, CurrentMotorBase = 1.f;
	TArray<FName> AllControls;

	FName StruckLimb;
    bool bEnergyLegReaction = false;
	float StateTime = 0.f;
	/** <= 0: stays LIMP until told otherwise. */
	float LimpDuration = 0.f;

	bool bReturning = false;
	float ReturnTime = 0.f;
	float ReturnDuration = 0.f;
	float PhysicsBlend = 0.f;
	float ReturnStartBlend = 1.f;

	bool bImpulsePending = false;
	FName PendingBone;
	FVector PendingPoint = FVector::ZeroVector;
	FVector PendingImpulse = FVector::ZeroVector;
	int32 PendingFrames = 0;

	/** This LIMP came from HandlePowerHit: wait for the body to settle before getting up (legacy hits keep fixed timing). */
	bool bWaitForSettle = false;

	/** A launch / shove velocity for every body, waiting for the bodies to start simulating. */
	bool bVelocityPending = false;
	FVector PendingVelocity = FVector::ZeroVector;
	/** Where the hit landed: body parts near it get more of PendingVelocity (the whip). */
	FVector PendingVelocityPoint = FVector::ZeroVector;
	int32 PendingVelocityFrames = 0;
};
