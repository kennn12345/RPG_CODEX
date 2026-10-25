#include "RPGFlightAnimationComponent.h"

#include "RPGCombatStateComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGAnimInstance.h"
#include "RPGDirectionalHoverComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGFlightAnimationComponent::URPGFlightAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
    BankL=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBankL.RPG_Proto_FlyBankL"));
    BankR=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBankR.RPG_Proto_FlyBankR"));
    BankFastL=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBankFastL.RPG_Proto_FlyBankFastL"));
    BankFastR=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBankFastR.RPG_Proto_FlyBankFastR"));
    Takeoff=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyTakeoff.RPG_Proto_FlyTakeoff"));
    Accelerate=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyAccelerate.RPG_Proto_FlyAccelerate"));
    BoostClip=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBoost.RPG_Proto_FlyBoost"));
    Release=FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyRelease.RPG_Proto_FlyRelease"));
	Hover = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyHover.RPG_Proto_FlyHover"));
	Forward = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyForward.RPG_Proto_FlyForward"));
	Brake = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyBrake.RPG_Proto_FlyBrake"));
	Fast = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyFast.RPG_Proto_FlyFast"));
}

void URPGFlightAnimationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement()) AddTickPrerequisiteComponent(Movement);
        if (USkeletalMeshComponent* Mesh = Character->GetMesh())
        {
            Mesh->AddTickPrerequisiteComponent(this);
            ResolveFlightClips(Mesh->GetAnimInstance());
        }
    }
}

void URPGFlightAnimationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && Character->GetMesh())
    {
        ResetBodyReaction(Character->GetMesh());
        if (URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Character->GetMesh()->GetAnimInstance()))
        {
            Anim->FlightWeight = 0.f;
            Anim->FlowBones.Reset(); Anim->FlowRotations.Reset();
            Anim->RearSway=FRPGFlightRearSway();
        }
    }
    Super::EndPlay(Reason);
}

void URPGFlightAnimationComponent::ResolveFlightClips(UAnimInstance* Instance)
{
    if (!Instance || !Instance->CurrentSkeleton) return;
    if (ResolvedSkeleton == Instance->CurrentSkeleton && ResolvedClips.Num() == RPGFlight::NumClips) return;
    ResolvedSkeleton = Instance->CurrentSkeleton;
    ResolvedClips.Reset();
    for (const TSoftObjectPtr<UAnimSequence>& Requested : {Hover, Forward, Fast, Brake, BankL, BankR, BankFastL, BankFastR, Takeoff, Accelerate, BoostClip, Release})
    {
        UAnimSequence* Clip = Requested.LoadSynchronous();
        if (!Clip || Clip->GetSkeleton() != ResolvedSkeleton)
        {
            const FString Name = Requested.GetAssetName();
            Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/RPG/Animations/%s/%s.%s"),
                *ResolvedSkeleton->GetName(), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
        }
        ResolvedClips.Add(Clip && Clip->GetSkeleton() == ResolvedSkeleton ? Clip : nullptr);
    }
}

void URPGFlightAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
    URPGAnimInstance* Anim = Mesh ? Cast<URPGAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
    if (!Movement || !Anim || DeltaTime <= 0.f) return;
    const bool bFlying = Movement->IsFlying();
    const bool bGrounded = Movement->IsMovingOnGround();
    if (bFlying || FlightWeight > 0.f) ResolveFlightClips(Anim);
    const float Dt = FMath::Min(DeltaTime, 0.1f); // stalls must not explode springs or acceleration
    const FVector Velocity = Movement->Velocity;
    const FVector Accel = bVelocityInitialized && bFlying == bWasFlying
        ? ((Velocity - FlowLastVelocity) / FMath::Max(DeltaTime, 0.001f)).GetClampedToMaxSize(6000.f) : FVector::ZeroVector;
    bVelocityInitialized = true;
    FlowLastVelocity = Velocity;
    FilteredAcceleration = FMath::Lerp(FilteredAcceleration, Accel, 1.f - FMath::Exp(-Dt / 0.12f));
    const URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
    const bool bCombat = State && (State->AttackPhase != ERPGAttackPhase::None || State->bIsChargingAttack || State->bIsBlocking);
    // GASP traversal can temporarily use MOVE_Flying too. Its root-motion montage
    // must own the pose and mesh while vaulting/mantling, rather than being painted over.
    const bool bTraversal = Character->IsPlayingRootMotion();
    const bool bYield = bCombat || bTraversal;
    bool bReady = ResolvedClips.Num() == RPGFlight::NumClips;
    for (const UAnimSequence* Clip : ResolvedClips) bReady &= Clip != nullptr;
    if (ActionIndex != INDEX_NONE)
    {
        if (ActionIndex == RPGFlight::Release && bReleaseReentry && bFlying)
        {
            // Keep the interrupted pose while it fades back into the flying base.
            ReleaseReentryBlend = FMath::FInterpConstantTo(ReleaseReentryBlend,0.f,Dt,1.f/FMath::Max(BlendTime,0.05f));
            if (ReleaseReentryBlend <= 0.f) { ActionIndex=INDEX_NONE; bReleaseReentry=false; }
        }
        else
        {
            ActionTime += Dt;
            ReleaseReentryBlend = FMath::FInterpConstantTo(ReleaseReentryBlend,1.f,Dt,1.f/FMath::Max(BlendTime,0.05f));
        }
        const UAnimSequence* Clip = ResolvedClips.IsValidIndex(ActionIndex) ? ResolvedClips[ActionIndex].Get() : nullptr;
        if (!Clip || (ActionTime >= Clip->GetPlayLength() && (ActionIndex != RPGFlight::Release || FlightWeight <= 0.f)))
        { ActionIndex=INDEX_NONE; bReleaseReentry=false; ReleaseReentryBlend=1.f; }
        else if (ActionIndex == RPGFlight::Release) ActionTime = FMath::Min(ActionTime,Clip->GetPlayLength());
    }
    if (!bWasFlying && bFlying)
    {
        const bool bResumeRelease=ActionIndex==RPGFlight::Release && FlightWeight>0.05f && ReleaseTravel>0.05f;
        if(bResumeRelease)bReleaseReentry=true;
        else StartFlightAction(RPGFlight::Takeoff);
        bAccelerationArmed = bBoostArmed = true;
        if(!bResumeRelease)
        {
            TurnPose = FilteredTurnRate = TurnRateRad = 0.f;
            bTravelHeadingValid = false;
            bVerticalFacingActive = false;
        }
        FilteredAcceleration = FVector::ZeroVector;
    }
    if (bWasFlying && !bFlying && !bYield && FlightWeight > 0.1f)
    {
        if(ActionIndex==RPGFlight::Release && bReleaseReentry)bReleaseReentry=false;
        else
        {
            ReleaseTravel = float(PoseBlend.X);
            StartFlightAction(RPGFlight::Release);
        }
    }
    const float TargetWeight = bFlying && !bYield && bReady ? 1.f : 0.f;
    const float Duration = TargetWeight > FlightWeight ? BlendTime
        : (bYield ? ActionBlendOutTime : (bGrounded ? GroundBlendOutTime : ExitBlendTime));
    const bool bReleaseHold = !bFlying && !bGrounded && !bYield && ActionIndex == RPGFlight::Release
        && ActionTime < ReleasePoseHoldTime * ReleaseTravel;
    const float PreviousFlightWeight = FlightWeight;
    FlightWeight = FMath::FInterpConstantTo(FlightWeight, TargetWeight, bReleaseHold ? 0.f : Dt, 1.f / FMath::Max(Duration, 0.05f));
    const float Speed = float(Velocity.Size());
    auto SmoothRange = [](float Value, float Low, float High)
    {
        const float T = FMath::Clamp((Value - Low) / FMath::Max(High - Low, 1.f), 0.f, 1.f);
        return T*T*(3.f-2.f*T);
    };
    const float Travel = SmoothRange(Speed, ForwardSpeed * 0.6f, ForwardFullSpeed);
    const float Boost = SmoothRange(Speed, FastSpeed * 0.8f, FMath::Max(FastSpeed * 1.45f, ForwardFullSpeed + 1.f));
    const float Deceleration = Speed > 40.f ? -float(FVector::DotProduct(FilteredAcceleration, Velocity.GetSafeNormal())) : 0.f;
    const float Braking = SmoothRange(Deceleration, 250.f, FMath::Max(FlapBrake, 251.f)) * SmoothRange(Speed, 60.f, 350.f);
    // Releasing movement should visibly catch momentum even when ordinary
    // flight's deceleration never reaches the old full-brake threshold. Retain
    // the catch briefly after stopping, then settle into the upright hover.
    const bool bStopping = bFlying && !bYield && Speed > 100.f
        && Movement->GetCurrentAcceleration().IsNearlyZero(10.f) && Deceleration > 250.f;
    if (bStopping) StopBraceHold = 0.24f;
    else StopBraceHold = FMath::Max(0.f,StopBraceHold-Dt);
    if (!bFlying || bYield || !Movement->GetCurrentAcceleration().IsNearlyZero(10.f)) StopBraceHold = 0.f;
    const float StopBrace = bStopping || StopBraceHold > 0.f ? 0.95f : 0.f;
    // Catch the deceleration quickly, then let the broad braking silhouette settle
    // after velocity reaches zero, instead of disappearing on the final stopping frame.
    const float BrakeTarget = bFlying && !bYield ? FMath::Max(Braking * 0.85f, StopBrace) : 0.f;
    BrakePoseWeight = FMath::Lerp(BrakePoseWeight, BrakeTarget,
        1.f - FMath::Exp(-Dt / FMath::Max(BrakeTarget > BrakePoseWeight ? BrakeBlendInTime : BrakeBlendOutTime,0.01f)));
    const FVector TargetBlend(Travel, Boost, BrakePoseWeight);
    // Entering flight with existing momentum must not briefly select upright
    // hover. The global flight weight still blends in from the falling graph.
    if (!bWasFlying && bFlying && !bReleaseReentry && Speed >= ForwardSpeed)
        PoseBlend = FVector(1.f,Boost,0.f);
    if (Travel < 0.03f) bAccelerationArmed = true;
    if (Boost < 0.03f) bBoostArmed = true;
    if (bFlying && !bYield && ActionIndex == INDEX_NONE)
    {
        if (bBoostArmed && Boost > 0.12f && Boost < 0.85f)
        {
            StartFlightAction(RPGFlight::Boost); bBoostArmed = false;
        }
        else if (bAccelerationArmed && Travel > 0.1f && Travel < 0.85f && Boost < 0.12f)
        {
            StartFlightAction(RPGFlight::Accelerate); bAccelerationArmed = false;
        }
    }
    if (Boost >= 0.85f) bBoostArmed = false;
    // Takeoff can already carry forward momentum. Do not defer a hover-to-cruise
    // action until takeoff ends after the character has reached cruise speed.
    if (Travel >= 0.85f) bAccelerationArmed = false;
    if (bFlying)
    {
        PoseBlend = FMath::Lerp(PoseBlend, TargetBlend, 1.f - FMath::Exp(-Dt / FMath::Max(PoseResponseTime, 0.05f)));
        PoseBlend.Z = BrakePoseWeight;
    }
    else if (FlightWeight > 0.f && ActionIndex != RPGFlight::Release)
        PoseBlend = FMath::Lerp(PoseBlend, FVector(0,0,0.35f), 1.f - FMath::Exp(-Dt / 0.23f));
    else if (FlightWeight <= 0.f) PoseBlend = FVector::ZeroVector;
    if (!bWasFlying && bFlying && !bReleaseReentry) Phase = 0.f;
    Phase = FMath::Fmod(Phase + Dt / 4.f, 1.f);
    Anim->FlightClips = ResolvedClips;
    Anim->FlightWeight = FlightWeight * FlightWeight * (3.f - 2.f * FlightWeight);
    Anim->FlightBlend = PoseBlend;
    Anim->FlightPhase = Phase;
    // Combat/traversal owns the whole mesh once the flight pose is released.
    // Use that same eased release, rather than leaving lean behind and then
    // reapplying stale Pitch/Roll when a short interruption resumes.
    const float PreviousPoseWeight = PreviousFlightWeight * PreviousFlightWeight * (3.f - 2.f * PreviousFlightWeight);
    const float PoseReleaseAlpha = bYield ? (PreviousPoseWeight > 1.e-6f
        ? FMath::Clamp(1.f - Anim->FlightWeight / PreviousPoseWeight, 0.f, 1.f) : 1.f) : -1.f;
    const auto* Directional=Character->FindComponentByClass<URPGDirectionalHoverComponent>();
    if (bFlying && !bYield && !(Directional && Directional->IsHovering())) UpdateBodyReaction(Mesh, Velocity, Dt);
    else ResetBodyReaction(Mesh, Dt, bGrounded, PoseReleaseAlpha);
    const float TargetTurn = bFlying && !bYield ? FMath::Clamp(
        (0.65f * FMath::RadiansToDegrees(TurnRateRad) + 0.35f * FilteredTurnRate) / FMath::Max(FullTurnPoseRate,1.f),-1.f,1.f)
        * FMath::Clamp(float(Velocity.Size2D()) / FMath::Max(ForwardFullSpeed,1.f),0.f,1.f) : 0.f;
    TurnPose = FMath::Lerp(TurnPose,TargetTurn,1.f-FMath::Exp(-Dt/0.16f));
    Anim->FlightTurn = TurnPose;
    Anim->FlightActionIndex = ActionIndex;
    Anim->FlightActionTime = ActionTime;
    float ActionWeight = 0.f;
    if (ActionIndex != INDEX_NONE && ResolvedClips.IsValidIndex(ActionIndex) && ResolvedClips[ActionIndex])
    {
        const float Length = ResolvedClips[ActionIndex]->GetPlayLength();
        const float InDuration = ActionIndex == RPGFlight::Accelerate ? FMath::Max(BlendTime,0.14f) : 0.14f;
        const float In = ActionIndex == RPGFlight::Takeoff ? 1.f : FMath::Clamp(ActionTime/InDuration,0.f,1.f);
        const float Out = ActionIndex == RPGFlight::Release ? 1.f : FMath::Clamp((Length-ActionTime)/0.20f,0.f,1.f);
        ActionWeight = In*In*(3.f-2.f*In)*Out*Out*(3.f-2.f*Out);
        // Upright takeoff belongs to hover. Let the existing travel blend own
        // the lean into flight as soon as movement starts.
        if (ActionIndex == RPGFlight::Takeoff)
            ActionWeight *= FMath::Square(1.f-FMath::Clamp(float(PoseBlend.X),0.f,1.f));
        // A hover release uses the existing falling graph; the authored release carries forward momentum.
        if (ActionIndex == RPGFlight::Release)
            ActionWeight *= ReleaseTravel * ReleaseReentryBlend * ReleaseReentryBlend * (3.f - 2.f * ReleaseReentryBlend);
        // Strong turns keep their authored bracing even if acceleration/boost overlaps.
        if (ActionIndex == RPGFlight::Accelerate || ActionIndex == RPGFlight::Boost)
            ActionWeight *= 1.f-0.75f*FMath::Abs(TurnPose);
        // A pickup from hover can reach boost while its ordinary acceleration
        // action is still playing. Let the fast pose take over progressively.
        if (ActionIndex == RPGFlight::Accelerate)
            ActionWeight *= 1.f-FMath::Clamp(float(PoseBlend.Y),0.f,1.f);
    }
    Anim->FlightActionWeight = ActionWeight;
    UpdateLimbFlow(Mesh, Anim, FilteredAcceleration, Velocity, bFlying && !bYield && bReady, Dt);
    if (URPGPhysicsReactionComponent* Physics = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>())
        Physics->SetFlightFollow(false); // intentionally never enable the rejected physics implementation
    bWasFlying = bFlying;
}

void URPGFlightAnimationComponent::StartFlightAction(int32 ClipIndex)
{
    bReleaseReentry = false;
    ReleaseReentryBlend = 1.f;
    ActionIndex = ClipIndex;
    ActionTime = 0.f;
}

void URPGFlightAnimationComponent::UpdateBodyReaction(USkeletalMeshComponent* Mesh, const FVector& Velocity, float DeltaTime)
{
	if (!Mesh || DeltaTime <= 0.f) return;
	const AActor* Owner = GetOwner();
	if (!bReacting)
	{
		bReacting = true;
		RestLocation = Mesh->GetRelativeLocation();
		RestRotation = Mesh->GetRelativeRotation().Quaternion();
		LastHeading = Velocity.Rotation().Yaw;
		Pitch = Roll = 0.f;
		SmoothedYaw = float(Owner->GetActorRotation().Yaw);
	}
	// The body turns after the view: a smoothed yaw that catches up with the actor's.
	const float ActorYaw = float(Owner->GetActorRotation().Yaw);
    const float Horizontal = float(Velocity.Size2D());
    const ACharacter* Character = Cast<ACharacter>(Owner);
    const AController* Controller = Character ? Character->GetController() : nullptr;
    // GASP's movement-facing yaw falls back to zero for a purely vertical input.
    // Retain look-facing through the following hover too: reaching zero vertical
    // speed must not turn the visible body back to that fallback heading.
    if (Horizontal >= 50.f) bVerticalFacingActive = false;
    else if (FMath::Abs(Velocity.Z)>1.f && Controller) bVerticalFacingActive = true;
    const float FacingYaw = bVerticalFacingActive && Controller
        ? float(Controller->GetControlRotation().Yaw) : ActorYaw;
	const float YawStep = FMath::FindDeltaAngleDegrees(SmoothedYaw, FacingYaw) * (1.f - FMath::Exp(-TurnFollowSpeed * DeltaTime));
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + YawStep);
	TurnRateRad = FMath::Clamp(FMath::DegreesToRadians(YawStep / DeltaTime), -3.5f, 3.5f);
	const float YawLag = FMath::FindDeltaAngleDegrees(ActorYaw, SmoothedYaw);
	const float React = FMath::Lerp(HoverReaction, 1.f, FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f));
	const FVector Accel = FilteredAcceleration;
	const float AccelForward = float(FVector::DotProduct(Accel, Owner->GetActorForwardVector()));
	// Climb / dive along the flight path, plus leaning into speeding up (nose down) and back when slowing.
	float WantPitch = FMath::RadiansToDegrees(FMath::Atan2(float(Velocity.Z), FMath::Max(Horizontal, 100.f))) * ClimbPitch * float(PoseBlend.X) * FMath::Lerp(CruisePathPitchScale,1.f,float(PoseBlend.Y));
	WantPitch -= FMath::Clamp(AccelForward / 1000.f * AccelLean, -15.f, 15.f);
	WantPitch = FMath::Clamp(WantPitch, -MaxPitch, MaxPitch);
    // The brake clip gathers the body toward upright. Let it replace the path
    // lean, otherwise a dive can cancel its lift and hide the braking transition.
    const float PathReaction = 1.f - FMath::Clamp(BrakePoseWeight, 0.f, 1.f);
    WantPitch *= PathReaction;
	// Bank into turns (how fast the travel direction swings, more at speed).
	const float Heading = Horizontal > 50.f ? float(Velocity.Rotation().Yaw) : LastHeading;
    // A heading is undefined while hovering or travelling vertically. The first
    // horizontal sample seeds it; it is not a turn from the old/zero heading.
	const float RawTurnRate = Horizontal > 50.f && bTravelHeadingValid
        ? FMath::Clamp(FMath::FindDeltaAngleDegrees(LastHeading, Heading) / DeltaTime, -240.f, 240.f) : 0.f;
    bTravelHeadingValid = Horizontal > 50.f;
    FilteredTurnRate = FMath::Lerp(FilteredTurnRate, RawTurnRate, 1.f - FMath::Exp(-DeltaTime / 0.16f));
    // The same signed demand drives the whole-body bank and the authored turn.
    // Independent side-force/shoulder layers used to stack different tilts.
    const float TurnRate = 0.65f * FMath::RadiansToDegrees(TurnRateRad) + 0.35f * FilteredTurnRate;
	float WantRoll = TurnRate * BankPerTurn * FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f);
	WantRoll = FMath::Clamp(WantRoll, -MaxBank, MaxBank);
    WantRoll *= PathReaction;
	Pitch = FMath::Lerp(Pitch, WantPitch * React, 1.f - FMath::Exp(-ReactionSpeed * DeltaTime));
	Roll = FMath::Lerp(Roll, WantRoll * React, 1.f - FMath::Exp(-ReactionSpeed * DeltaTime));
	LastHeading = Heading;

	// Turn about the hips, in the actor's frame.
	const FQuat Lean = FQuat(FVector::UpVector, FMath::DegreesToRadians(YawLag)) * FRotator(Pitch, 0.f, Roll).Quaternion();
	const FVector Pivot = RestLocation + FVector(0.f, 0.f, PivotHeight);
    // A partially restored mesh can be slightly off this pivot arc. Carry that
    // residual position into reentry and settle it, instead of snapping it away.
    const FQuat PreviousLean=Mesh->GetRelativeRotation().Quaternion()*RestRotation.Inverse();
    const FVector RestoreOffset=Mesh->GetRelativeLocation()-(Pivot+PreviousLean.RotateVector(RestLocation-Pivot));
	Mesh->SetRelativeLocationAndRotation(Pivot + Lean.RotateVector(RestLocation - Pivot)
        + RestoreOffset*FMath::Exp(-ReactionSpeed*DeltaTime), Lean * RestRotation);
}

void URPGFlightAnimationComponent::ResetBodyReaction(USkeletalMeshComponent* Mesh, float DeltaTime, bool bGrounded, float PoseReleaseAlpha)
{
    bTravelHeadingValid = false;
    bVerticalFacingActive = false;
    if (!bReacting || !Mesh) return;
    TurnRateRad = FilteredTurnRate = 0.f;
    const float RestoreTime = bGrounded ? GroundBlendOutTime * 0.45f : ExitBlendTime * 0.25f;
    const float Alpha = PoseReleaseAlpha >= 0.f ? FMath::Clamp(PoseReleaseAlpha, 0.f, 1.f)
        : (DeltaTime > 0.f ? 1.f - FMath::Exp(-DeltaTime / FMath::Max(RestoreTime, 0.025f)) : 1.f);
    const FVector Location = FMath::Lerp(Mesh->GetRelativeLocation(), RestLocation, Alpha);
    const FQuat Rotation = FQuat::Slerp(Mesh->GetRelativeRotation().Quaternion(), RestRotation, Alpha).GetNormalized();
    Mesh->SetRelativeLocationAndRotation(Location, Rotation);
    // Restoration may be interrupted by reentry before it reaches rest. Resume
    // from the applied lean, rather than the stale pre-interruption state.
    const FRotator RestoredLean = (Rotation * RestRotation.Inverse()).Rotator();
    Pitch = float(RestoredLean.Pitch);
    Roll = float(RestoredLean.Roll);
    SmoothedYaw = FRotator::NormalizeAxis(float(GetOwner()->GetActorRotation().Yaw) + float(RestoredLean.Yaw));
    if (Alpha == 1.f || (Location.Equals(RestLocation, 0.01f) && Rotation.Equals(RestRotation, 0.0001f)))
    {
        Mesh->SetRelativeLocationAndRotation(RestLocation, RestRotation);
        bReacting = false;
    }
}

namespace
{
    // The bank clips own the shoulders, elbows and torso. Only small inertial
    // swings remain on the four limbs, so no second steering layer can shrug a shoulder.
    struct FFlowJoint { const TCHAR* Bone; const TCHAR* Child; float Gain; float Stiffness; float Side; };
    const FFlowJoint FlowJoints[] = {
        { TEXT("thigh_l"), TEXT("calf_l"), 1.5f, 0.85f, 1.f },
        { TEXT("thigh_r"), TEXT("calf_r"), 1.5f, 0.85f, -1.f },
        { TEXT("upperarm_l"), TEXT("lowerarm_l"), 0.45f, 1.5f, 1.f },
        { TEXT("upperarm_r"), TEXT("lowerarm_r"), 0.45f, 1.5f, -1.f },
    };
}

void URPGFlightAnimationComponent::UpdateLimbFlow(USkeletalMeshComponent* Mesh, UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime)
{
    URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Instance);
    if (!Mesh || !Anim) return;
    const int32 Count = UE_ARRAY_COUNT(FlowJoints);
    if (FlowAngle.Num() != Count)
    {
        FlowAngle.Init(FVector::ZeroVector,Count);
        FlowSpeed.Init(FVector::ZeroVector,Count);
        FlowDir.Init(FVector::ZeroVector,Count);
    }
    FlowWeight = FMath::FInterpConstantTo(FlowWeight,bFlying && bLimbFlow ? Anim->FlightWeight : 0.f,DeltaTime,5.f);
    if (FlowWeight <= 0.f)
    {
        for (int32 I=0; I<Count; ++I) FlowAngle[I]=FlowSpeed[I]=FlowDir[I]=FVector::ZeroVector;
        Anim->FlowBones.Reset(); Anim->FlowRotations.Reset();
        RearAngle=RearVelocity=FVector2D::ZeroVector;RearKneeAngle=RearKneeVelocity=0.f;
        Anim->RearSway=FRPGFlightRearSway();
        return;
    }
    const FTransform& ToMesh = Mesh->GetComponentTransform();
    const FVector A = ToMesh.InverseTransformVectorNoScale(Accel.GetClampedToMaxSize(6000.f))/1000.f;
    const FVector V = ToMesh.InverseTransformVectorNoScale(Velocity)/1000.f;
    const float Speed = FMath::Clamp(float(Velocity.Size())/FMath::Max(FastSpeed,1.f),0.f,1.f);
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime/(1.f/120.f)),1,8);
    const float Dt = DeltaTime/Steps;
    const auto* Directional=GetOwner()->FindComponentByClass<URPGDirectionalHoverComponent>();
    const bool bTravel=bFlying && !(Directional && Directional->IsHovering());
    const float RearLimit=FMath::DegreesToRadians(FMath::Clamp(RearSwayMaxAngle,0.f,15.f));
    const FVector CurveWS=FVector::CrossProduct(Velocity,Accel)/FMath::Max(Velocity.SizeSquared(),40000.);
    const float PitchRate=float(FVector::DotProduct(CurveWS,GetOwner()->GetActorRightVector()));
    const FVector2D WantRear=bTravel?FVector2D(
        FMath::Clamp(-TurnRateRad*RearTurnLag,-RearLimit,RearLimit),
        FMath::Clamp(-PitchRate*RearTurnLag*.6f,-RearLimit*.65f,RearLimit*.65f)):FVector2D::ZeroVector;
    const float RearK=FMath::Max(RearSwayStiffness,1.f),RearC=2.f*RearSwayDamping*FMath::Sqrt(RearK);
    const float KneeTarget=bTravel?FMath::DegreesToRadians(RearKneeFlex)*FMath::Clamp(float(FMath::Abs(RearAngle.X))/FMath::Max(RearLimit,.001f),0.f,1.f):0.f;
    for(int32 S=0;S<Steps;++S)
    {
        RearVelocity+=((WantRear-RearAngle)*RearK-RearVelocity*RearC)*Dt;
        RearAngle+=RearVelocity*Dt;
        // A slower hinge response lets the shins catch up after the hips begin a turn.
        RearKneeVelocity+=((KneeTarget-RearKneeAngle)*36.f-RearKneeVelocity*10.2f)*Dt;
        RearKneeAngle+=RearKneeVelocity*Dt;
    }
    RearAngle.X=FMath::Clamp(RearAngle.X,-RearLimit,RearLimit);
    RearAngle.Y=FMath::Clamp(RearAngle.Y,-RearLimit*.65f,RearLimit*.65f);
    const bool bEntryOrRelease=Anim->FlightActionIndex==RPGFlight::Takeoff || Anim->FlightActionIndex==RPGFlight::Release;
    const float RearWeight=FlowWeight*float(Anim->FlightBlend.X)*(1.f-float(Anim->FlightBlend.Z))*(bEntryOrRelease?1.f-Anim->FlightActionWeight:1.f);
    Anim->RearSway.WaistRotationCS=(ToMesh.InverseTransformVectorNoScale(FVector::UpVector)*RearAngle.X
        +ToMesh.InverseTransformVectorNoScale(GetOwner()->GetActorRightVector())*RearAngle.Y)*RearWeight;
    Anim->RearSway.KneeFlexion=FMath::Clamp(RearKneeAngle,0.f,FMath::DegreesToRadians(RearKneeFlex))*RearWeight;
    // Read last frame's applied rotation below. Newly allocated entries must be
    // zero, including the first frame after flight has released these arrays.
    const TArray<FVector> PreviousRotations=Anim->FlowRotations;
    Anim->FlowBones.SetNum(Count); Anim->FlowRotations.SetNumZeroed(Count);
    for (int32 I=0; I<Count; ++I)
    {
        const FFlowJoint& Joint = FlowJoints[I];
        FVector Direction = Mesh->GetBoneLocation(FName(Joint.Child),EBoneSpaces::ComponentSpace)
            - Mesh->GetBoneLocation(FName(Joint.Bone),EBoneSpaces::ComponentSpace);
        const FVector Applied = PreviousRotations.IsValidIndex(I)?PreviousRotations[I]:FVector::ZeroVector;
        if (!Applied.IsNearlyZero()) Direction=FQuat(Applied.GetSafeNormal(),float(Applied.Size())).UnrotateVector(Direction);
        Direction=Direction.GetSafeNormal(SMALL_NUMBER,FVector(0,0,-1));
        const float Alpha=1.f-FMath::Exp(-DeltaTime/0.12f);
        FlowDir[I]=FlowDir[I].IsNearlyZero() ? Direction : FMath::Lerp(FlowDir[I],Direction,Alpha).GetSafeNormal();
        Direction=FlowDir[I];
        const float K=FlowStiffness*Joint.Stiffness*FMath::Lerp(1.f,FlowStiffnessAtSpeed,Speed);
        const bool bLeg=I<2;
        const float C=2.f*FlowDamping*(bLeg?.85f:1.f)*FMath::Sqrt(K);
        const float Limit=FMath::DegreesToRadians(bLeg?FMath::Clamp(LegFlowMaxAngle,0.f,12.f):FMath::Min(FlowMaxAngle,8.f));
        const FVector Push=Joint.Gain*(FlowInertia*FVector::CrossProduct(Direction,-A)
            + FlowDrag*Speed*FVector::CrossProduct(Direction,-V)
            - FlowTurn*TurnRateRad*FVector::UpVector*10.f);
        for (int32 S=0; S<Steps; ++S)
        {
            FlowSpeed[I]+=(Push-K*FlowAngle[I]-C*FlowSpeed[I])*Dt;
            FlowAngle[I]+=FlowSpeed[I]*Dt;
        }
        if (FlowAngle[I].Size()>Limit)
        {
            FlowAngle[I]=FlowAngle[I].GetSafeNormal()*Limit;
            FlowSpeed[I]*=0.5f;
        }
        const FVector OutAxis=FVector::CrossProduct(Direction,FVector(Joint.Side,0,0)).GetSafeNormal();
        const float Inward=-float(FVector::DotProduct(FlowAngle[I],OutAxis));
        if (Inward>0.03f)
        {
            FlowAngle[I]+=OutAxis*(Inward-0.03f);
            const float InwardSpeed=-float(FVector::DotProduct(FlowSpeed[I],OutAxis));
            if (InwardSpeed>0.f) FlowSpeed[I]+=OutAxis*InwardSpeed;
        }
        Anim->FlowBones[I]=FName(Joint.Bone);
        Anim->FlowRotations[I]=FlowAngle[I]*FlowWeight;
    }
}
