#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGEnergySpellTypes.h"
#include "RPGPlayerTypes.h"
#include "RPGPowerTypes.h"
#include "RPGHitOutcomeComponent.generated.h"

class ACharacter;
class UAnimSequence;
class USoundBase;

/**
 * What a hit does to a character's footing, shared by NPCs and the player (the Power System picks the outcome; this keeps
 * the state around it and plays the parts every humanoid shares):
 *   - a hidden BALANCE meter: each hit drains it, it refills a moment after the last one, and a rocked body reacts harder
 *     (quick follow-ups escalate stagger -> knockback -> catch / knockdown);
 *   - the SITUATION it was hit in: planted in the fighting stance (harder to move), mid-attack, running, hit from behind;
 *   - BRACING: GASP's ragdoll reach poses on the upper body (arms out toward the fall) while it stumbles and catches
 *     itself, braces on a wall, or falls; a body shot doubles it over;
 *   - every knockdown is an active ragdoll that fights the fall (URPGPhysicsReactionComponent::SetRagdollBrace) unless it
 *     is knocked out;
 *   - for an owner without reactions of its own (the player): the whole response - pushes, a catch, a braced knockdown
 *     through URPGBodyImpactComponent, camera shake - and hits while it lies on the ground.
 * NPCs (ARPGNpcCharacter) use the state and bracing here with their own stepping reactions.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class RPG_CODEX_API URPGHitOutcomeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGHitOutcomeComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 0-1: what is left of its balance (1 = steady). */
	UFUNCTION(BlueprintPure, Category = "RPG|Hit Outcome")
	float GetBalance() const { return Balance; }

	/** How its situation scales a hit from StrikeDirection (Power settings > Balance); bOutFromBehind: struck in the back. */
	float GetSituation(const FVector& StrikeDirection, bool* bOutFromBehind = nullptr) const;

	/** Fills the target side of a Power input: balance and situation. */
	void PrepareInput(FRPGPowerInput& Input) const;

	/** A hit landed: drains balance by its reaction level (a bump by its reaction). */
	void NoteHit(const FRPGPowerResult& Result);
	void NoteBump(ERPGPowerReaction Reaction);

	/** Arms out toward FallDirection (world) on the upper body: Weight, held Hold seconds, then fades over Fade. */
	void StartBrace(const FVector& FallDirection, float Weight, float Hold, float Fade);
	/** Doubled over (a body shot): the spine folds forward Degrees, held then fading. */
	void StartBend(float Degrees, float Hold, float Fade);

	/** Is it out cold (no health left)? A knockdown then falls fully limp. */
	bool IsKnockedOut() const;

	/**
	 * Knee-down catch (hand-keyed clips RPG_Catch_Knee{Fwd,Back}_{Recover,Fall}_{Soft,Mid,Hard}): for a fall forward or
	 * back (up to KneeCatchAlignment off straight; the body turns into the fall as it stumbles) it stumbles a step and
	 * drops onto a knee with a hand braced on the front knee - always for a leg hit (the knee buckles), else KneeCatchShare
	 * of the time. The momentum picks the clip: the hit's force (its reaction level across the catch band) plus how fast
	 * the body was already moving that way choose Soft / Mid / Hard (a shorter, slower stumble or a longer, faster one),
	 * and the play rate is tuned within the band. Root motion carries the body. bFalls = the catch fails: as the clip runs
	 * out the body goes down in the braced ragdoll carrying the hips' actual velocity. Returns the montage, or null when
	 * this catch isn't a knee-down one (the arms-out stumble instead).
	 */
	class UAnimMontage* TryKneeCatch(const FVector& FallDirection, bool bFalls, const FRPGPowerResult& Result);
	/** 0-1: how hard the body is being driven over (the catch band of the reaction level + its own speed that way). */
	float CatchIntensity(const FRPGPowerResult& Result, const FVector& FallDirection) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0", ClampMax = "1"))
	float KneeCatchShare = 0.5f;
	/** How square to the body (forward / back) a fall must be for a knee-down catch (cosine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0", ClampMax = "1"))
	float KneeCatchAlignment = 0.5f;
	/** Seconds the body takes to turn square into the fall at the start of a knee-down catch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0.01"))
	float KneeTurnTime = 0.25f;
	/** What the last knee-down catch chose (tests, debug). */
	FName LastKneeClip;
	float LastKneeRate = 1.f;
	float LastKneeIntensity = 0.f;
	FVector LastKneeFallVelocity = FVector::ZeroVector;
	/** Tests: 1 = always a knee-down catch when the fall allows one, -1 = never, 0 = by the rules above. */
	int32 TestKneeCatch = 0;
	int32 KneeCatchCount = 0;
	int32 KneeFallCount = 0;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveKneeCatch;

	/** About to go down: arms brace toward FallDirection and the ragdoll fights the fall, unless knocked out (fully limp). */
	void BeginFall(const FVector& FallDirection, bool bKnockedOut);

	/** The whole response for an owner without reactions of its own (the player). */
	void ReceiveMeleeHit(const FRPGMeleeHitInfo& Hit);
	void ReceiveEnergyHit(const FRPGEnergyImpact& Impact);
	void ReceiveBump(const FVector& Velocity, ERPGPowerReaction Reaction);

	/** A body hitting the ground / a wall (GASP's ragdoll foley). */
	static void PlayImpactSound(const AActor* Owner, const FVector& Location, float Volume = 1.f);

	// ---- Player response (owners without reactions of their own) ----

	/** Push speed (cm/s) per cm/s of the Power System's displacement velocity, and the most a push can be. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0"))
	float PushScale = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0"))
	float MaxPush = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0"))
	float KnockbackPush = 500.f;
	/** A failed catch stumbles this long (s) before it goes down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0"))
	float CatchFailDelay = 0.35f;
	/** Health share at or below which a knockdown falls fully limp (knocked out cold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Hit Outcome", meta = (ClampMin = "0", ClampMax = "1"))
	float LimpHealthShare = 0.05f;

	/** Tests: nonzero = every hit on this character rolls with this seed (pick a recovered or a failed catch). */
	int32 TestSeed = 0;

	// ---- What happened (tests, debug) ----
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Hit Outcome")
	FRPGPowerResult LastResult;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Hit Outcome")
	int32 HitCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "RPG|Hit Outcome")
	bool bLastFromBehind = false;
	/** Peak upper-body brace weight / fold since ResetPeaks (tests). */
	float PeakBrace = 0.f;
	float PeakBend = 0.f;
	int32 FallCount = 0;
	int32 LimpFallCount = 0;
	void ResetPeaks() { PeakBrace = PeakBend = 0.f; }
	float GetBraceWeight() const { return BraceWeight; }

private:
	ACharacter* GetCharacter() const;
	class URPGPhysicsReactionComponent* GetReaction() const;
	/** Applies Result to the player-like owner: push / catch / fall. */
	void ApplyResult(const FRPGPowerResult& Result, const FVector& Direction, const FVector& Point, FName Bone);
	void Push(FVector Velocity);
	void Fall(const FVector& Velocity, const FVector& Point);
	void Shake(float Scale) const;
	void WriteBrace();

	float Balance = 1.f;
	float LastHitTime = -100.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> ReachClips;   // forward, back, left, right
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> KneeClips;    // [direction (fwd, back)][outcome (recover, fall)][strength (soft, mid, hard)]
	FTimerHandle KneeFallTimer;
	/** Turning into the fall, and the hips' velocity (the momentum a failed catch falls with). */
	float KneeTurnFrom = 0.f, KneeTurnTo = 0.f, KneeTurnElapsed = 1.f;
	FVector LastPelvis = FVector::ZeroVector;
	FVector PelvisVelocity = FVector::ZeroVector;
	bool bHavePelvis = false;
	UAnimSequence* BraceA = nullptr;
	UAnimSequence* BraceB = nullptr;
	float BraceAlpha = 0.f;
	float BraceTarget = 0.f, BraceWeight = 0.f, BraceHold = 0.f, BraceFade = 0.5f;
	float BendTarget = 0.f, Bend = 0.f, BendHold = 0.f, BendFade = 0.5f;
	/** Falling braced: the overlay stays on while the ragdoll is limp, off the moment it starts getting up. */
	bool bFallBrace = false;
	float FallStartTime = -100.f;
	/** Knocked out cold (player): restored to full health once it is back up. */
	bool bOutCold = false;
	FTimerHandle CatchFailTimer;
};
