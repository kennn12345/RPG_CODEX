#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGPlayerTypes.h"
#include "RPGPrototypeAnimationComponent.generated.h"

class UAnimSequence;
class UAnimMontage;

/** Sample-pack animation playback. Keeps the character's locomotion AnimBlueprint active. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class RPG_CODEX_API URPGPrototypeAnimationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGPrototypeAnimationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * Punches thrown while moving play on the UpperBody layer so the legs keep the locomotion;
     * standing still they play full body (footwork, weight shift). Ground speed above this counts as moving.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation", meta=(ClampMin="0"))
    float MovingPunchSpeed = 20.f;

    /**
     * Normal grounded kicks (Kick / Heavy / Power, not Blitz) own the whole body: movement input is ignored and the
     * current ground speed bleeds off linearly over this long, about the kick's blend-in, so the feet plant as
     * the capsule stops instead of skating.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation", meta=(ClampMin="0.01"))
    float KickStopTime = 0.15f;

    /**
     * Movement input comes back this long before the kick ends. Both front kicks put the kicking foot back down
     * about 0.1 s before their end; releasing earlier would move the capsule while that foot is still in the air.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation", meta=(ClampMin="0"))
    float KickRecoveryUnlockTime = 0.1f;

    /**
     * Charge hold. Pressing RT / LT / Y starts the regular attack. Punches: the tap punch runs to its load key (its first
     * small load from the guard) and waits there; once the press is longer than a tap it continues in its Charged clip,
     * which slowly rears the body back from the load key to full coil (HoldStart, about when the charge is full), then
     * repeats a breathing loop [HoldStart, HoldEnd) while the button is held. Release jumps to HoldEnd (the same pose; a
     * release while still rearing back crossfades there) and the charged strike plays from the coil. Taps never leave the
     * tap clip. Kicks stop at their hold time inside the regular clip. All times come from Tools/AnimImport/mocap_timings.json (written by compose.py).
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightPunchChargeSwitchTime = 0.0667f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftPunchChargeSwitchTime = 0.0667f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightPunchHoldStart = 0.8f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightPunchHoldEnd = 1.7f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftPunchHoldStart = 0.8f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftPunchHoldEnd = 1.7f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightKickHoldTime = 0.2f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftKickHoldTime = 0.2f;
    /** Kicks hold the same way as punches: at their hold time they switch to their Charged clip, which breathes in the
     *  locked stance over [HoldTime, HoldEnd) while Y is held. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightKickHoldEnd = 1.1f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftKickHoldEnd = 1.1f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightKickChargedImpactTime = 1.24f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftKickChargedImpactTime = 1.24f;

    /** Impact time in each clip, where AttackPhase goes from Strike to Recovery. From mocap_timings.json. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightPunchImpactTime = 0.23f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftPunchImpactTime = 0.14f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightPunchChargedImpactTime = 1.84f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftPunchChargedImpactTime = 1.94f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float RightKickImpactTime = 0.34f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Charge", meta=(ClampMin="0"))
    float LeftKickImpactTime = 0.34f;

    /**
     * Melee hit window around each strike's impact time (the same window for the tap and Charged clips), in seconds
     * before and after it. Only while it is open does the limb register hits (bStrikeHitWindowOpen on the combat
     * state, read by URPGMeleeHitComponent); the windup, the charge hold and the recovery never hit.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Hit Window", meta=(ClampMin="0"))
    float PunchHitWindowBeforeImpact = 0.07f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Hit Window", meta=(ClampMin="0"))
    float PunchHitWindowAfterImpact = 0.05f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Hit Window", meta=(ClampMin="0"))
    float KickHitWindowBeforeImpact = 0.10f;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Hit Window", meta=(ClampMin="0"))
    float KickHitWindowAfterImpact = 0.08f;

    /** A close target is met before the animated impact: from this long before it, the limb landing on a target
     *  (URPGStrikeContactComponent::HasStrikingContact) opens the hit window right then. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|Hit Window", meta=(ClampMin="0"))
    float EarlyContactLead = 0.3f;


    /**
     * Between attacks the fighter stays in the fighting stance every attack starts from (looping CombatStance clip)
     * instead of dropping to the relaxed idle, until this many seconds pass with no punch, kick or guard. Standing still
     * it plays full body (the stance's feet); moving, on the upper body only so the legs keep the locomotion.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation", meta=(ClampMin="0"))
    float CombatStanceTimeout = 8.f;

    /**
     * First person, guard held while moving: share of the locomotion's chest motion (the walk / run bob, dip and lean,
     * relative to where the guard clip holds the chest) the shoulders cancel, so the block keeps its place in view
     * (the camera follows the body only a little). 0 = the hands ride the full locomotion motion; 1 = fixed in view.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation", meta=(ClampMin="0", ClampMax="1"))
    float FirstPersonGuardSteady = 0.75f;

    /** In-place prototype clips on SK_Mannequin. Root is never animated, so the character stays upright. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> RightPunch;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> LeftPunch;
    /** Held-punch versions: the tuned punch's start, a cocked hold loop, then the tuned strike. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> RightPunchCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> LeftPunchCharged;
    /** Held-kick versions: the tap kick with a breathing loop at its hold frame. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> RightFrontKickCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> LeftFrontKickCharged;
    /** Looping fighting stance held between attacks. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> CombatStance;
    /** Looping held pose, played while RT + LT are held unarmed. */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> UnarmedGuard;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> RightFrontKick;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> LeftFrontKick;
    /**
     * First-person presentation of the clips above (RPG_FP_*, from Tools/ProtoAnim/fpanim.py): the same keys and timing,
     * the hands moved into view from the eyes. While first person is on, every clip played is swapped for its version
     * here; charge, phase, impact and hit-window logic never change. The striking limb is identical inside the hit
     * window, so hits land the same in both views. An empty entry falls back to the third-person clip.
     */
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonRightPunch;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonLeftPunch;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonRightPunchCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonLeftPunchCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonRightFrontKick;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonLeftFrontKick;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonRightFrontKickCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonLeftFrontKickCharged;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonUnarmedGuard;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation|First Person")
    TSoftObjectPtr<UAnimSequence> FirstPersonCombatStance;

    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> FlyingAttack;
    UPROPERTY(EditAnywhere, Category="RPG|Prototype Animation")
    TSoftObjectPtr<UAnimSequence> FlightStart;

    /** Editor-only implementation, invoked by the supplied setup script. */
    UFUNCTION(BlueprintCallable, Category="RPG|Prototype Animation")
    static FString PreparePrototypeAnimations();

    /**
     * Editor-only: adds an "UpperBody" slot to ABP_Unarmed, layered from spine_01 up over the
     * existing DefaultSlot output, so upper-body clips (guard, moving punches) play while the legs keep the
     * locomotion. Safe to re-run: on an existing layer it only updates the blend mask.
     */
    UFUNCTION(BlueprintCallable, Category="RPG|Prototype Animation")
    static FString SetupUpperBodySlot();

    /**
     * Editor-only, used by Tools/ProtoAnim/ue_import.py: compress a clip at the rate it was baked at instead of
     * the project default (30 fps), so fast snaps keep their keys. The property is read-only to Python.
     */
    UFUNCTION(BlueprintCallable, Category="RPG|Prototype Animation")
    static bool SetClipSampleRate(UAnimSequence* Sequence, int32 FramesPerSecond);

    /**
     * Switch the clips' presentation between third and first person (called by the camera mode). Whatever is playing
     * continues from the same frame in the other version, paused if it was paused, so a perspective switch mid-attack
     * or mid-charge keeps its timing.
     */
    UFUNCTION(BlueprintCallable, Category="RPG|Prototype Animation")
    void SetFirstPersonPresentation(bool bFirstPerson);

    UFUNCTION(BlueprintPure, Category="RPG|Prototype Animation")
    bool IsFirstPersonPresentation() const { return bFirstPersonPresentation; }

    /**
     * Drives the first-person shadow body (a second, shadow-only copy of the Animation Blueprint): it plays the same
     * montages as the visible body, at the same position, paused / stopped with it, but with the approved
     * third-person clips, so the world sees a believable full-body pose of the same action. Presentation only: no
     * combat logic runs on it. Called by that body's anim instance every frame, before it animates.
     */
    void MirrorMontagesTo(UAnimInstance* Shadow);

    /** The third-person (source) clip of a clip being played (itself if it is one). */
    UAnimSequence* SourceClip(UAnimSequence* Sequence) const;

    /** Fighting: an attack or the guard is up, or one happened within CombatStanceTimeout (the combat stance is held). */
    UFUNCTION(BlueprintPure, Category="RPG|Prototype Animation")
    bool IsInCombat() const;

    /**
     * Editor-only: makes URPGAnimInstance the parent class of ABP_Unarmed (the graph is untouched), so the attack aim can
     * turn the spine after the graph. Safe to re-run.
     */
    UFUNCTION(BlueprintCallable, Category="RPG|Prototype Animation")
    static FString SetupAimAnimInstance();

private:
    UFUNCTION()
    void HandleAction(const FRPGPlayerActionInfo& Info);
    bool IsMovingOnGround() const;
    void HandOffStandingPunch(UAnimInstance* Instance);
    void LockMovementForKick();
    void UnlockKickMovement();
    void UpdateCharge(UAnimInstance* Instance, float DeltaTime);
    /** Lets a charging attack go on its release action. False if there is no matching charge (play it normally). */
    bool ReleaseCharge(UAnimInstance* Instance, const FRPGPlayerActionInfo& Info, bool bKick);
    void ClearCharge();
    /** Held past the punch's load key: continue in its Charged clip from the same frame. */
    void SwitchToChargedClip(UAnimInstance* Instance, float Position);
    void UpdateCombatStance(UAnimInstance* Instance);
    void StopCombatStance(UAnimInstance* Instance, float BlendTime);
    void SetAttackPhase(ERPGAttackPhase Phase);
    /** Opens the melee hit window while Position is near the strike's impact time, closes it otherwise. */
    void UpdateStrikeWindow(float Position);
    void CloseStrikeWindow();
    class URPGCombatStateComponent* GetCombatState() const;
    /** The version of a (third-person) clip to play in the current perspective. */
    UAnimSequence* Present(UAnimSequence* Sequence) const;
    /** Third-person clip -> first-person clip, filled when first person is first used. */
    void BuildPresentationMap(const USkeleton* Skeleton);

    bool bFirstPersonPresentation = false;
    /** Visible-body montage -> the shadow body's montage mirroring it. */
    UPROPERTY(Transient)
    TMap<TObjectPtr<UAnimMontage>, TObjectPtr<UAnimMontage>> ShadowMontages;
    UPROPERTY(Transient)
    TMap<TObjectPtr<UAnimSequence>, TObjectPtr<UAnimSequence>> FirstPersonClips;

    /** The punch / kick montage started on press, its hold and impact times, and whether its button is still down. */
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> ChargeMontage;
    float ChargeHoldTime = 0.f;
    float ChargeImpactTime = 0.f;
    bool bChargeIsKick = false;
    ERPGHand ChargeHand = ERPGHand::None;
    /** The limb the started punch / kick lands with. */
    ERPGStrikeLimb ChargeLimb = ERPGStrikeLimb::None;
    bool bChargeButtonHeld = false;
    bool bChargePaused = false;
    /** Punch only, until the switch: the Charged clip to continue in, and where its hold loop and impact are. */
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> ChargedSequence;
    float ChargeSwitchTime = 0.f;
    float ChargedHoldStart = 0.f;
    float ChargedHoldEnd = 0.f;
    float ChargedImpactTime = 0.f;
    /**
     * Punch held at its load key but not yet past the tap time (TapMaxTime on the input component, reported as
     * bIsChargingAttack): it waits there, paused, so a tap can still release into the tuned punch from the same frame.
     */
    bool bWaitingAtLoad = false;
    /** The Charged clip once switched to (for the crossfade when released while still rearing back). */
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> ChargedPlaying;
    /** Playing the Charged clip: the hold is its breathing loop rather than a pause. */
    bool bChargeLoops = false;
    /** This strike's window was opened early by the limb landing on a close target (held until the usual end). */
    bool bWindowOpenedByContact = false;
    /** The punch plays on the UpperBody layer (thrown while moving, or handed over when movement began). */
    bool bChargeUpperBody = false;
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> CombatStanceMontage;
    /** The guard montage, kept through its blend-out (GuardMontage clears on release) for the first-person steadying. */
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> SteadyGuardMontage;
    void UpdateGuardSteady(UAnimInstance* Instance);
    /** Game time of the last punch / kick / guard; the combat stance holds for CombatStanceTimeout after it. */
    float LastCombatActionTime = -1000.f;
    /** The combat stance is playing full body (standing still: the stance's legs) rather than on the upper body. */
    bool bCombatStanceFullBody = false;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> FullBodyPunch;
    /** The kick montage holding the movement lock, while bKickMovementLocked. */
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> KickMontage;
    bool bKickMovementLocked = false;
    /** Braking settings replaced while a kick bleeds off the ground speed, restored on unlock. */
    bool bSavedSeparateBrakingFriction = false;
    float SavedBrakingFriction = 0.f;
    float SavedBrakingDecelerationWalking = 0.f;
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> ActiveMontage;
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> GuardMontage;
};
