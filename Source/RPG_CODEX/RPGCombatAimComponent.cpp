#include "RPGCombatAimComponent.h"

#include "RPGAnimInstance.h"
#include "RPGMeleeHitComponent.h"
#include "RPGStrikeContactComponent.h"
#include "RPGCameraModeComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

URPGCombatAimComponent::URPGCombatAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URPGCombatAimComponent::BeginPlay()
{
	Super::BeginPlay();
	// The body's facing and the torso aim must be set before the mesh evaluates its pose for the frame.
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && Character->GetMesh())
		Character->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
}

URPGCombatStateComponent* URPGCombatAimComponent::GetCombatState() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
}

void URPGCombatAimComponent::GetView(const ACharacter* Character, FVector& OutLocation, FRotator& OutRotation) const
{
	// The camera's position (last frame's: it is placed after the animation), but this frame's look direction: the
	// camera always looks along the control rotation, so this is exactly where the reticle points.
	const APlayerController* Player = Cast<APlayerController>(Character->GetController());
	if (Player && Player->PlayerCameraManager) OutLocation = Player->PlayerCameraManager->GetCameraLocation();
	else Character->GetController()->GetPlayerViewPoint(OutLocation, OutRotation);
	OutRotation = Character->GetControlRotation();
}

FVector URPGCombatAimComponent::FindAimPoint(const ACharacter* Character, const FVector& ViewLocation, const FVector& ViewDirection,
	bool bFirstPerson, bool& bOutOnTarget) const
{
	const FTransform& MeshToWorld = Character->GetMesh()->GetComponentTransform();
	const FVector Pivot = MeshToWorld.TransformPosition(StrikePivot);
	const FVector Natural = MeshToWorld.TransformPosition(NaturalStrikePoint);

	// The first visible thing under the reticle, if it is within reach of the character (not the far wall, not the
	// character). Movement capsules are skipped: the reticle is on the body inside one, not on the invisible cylinder.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGCombatAim), false, Character);
	FHitResult Hit;
	const FVector End = ViewLocation + ViewDirection * (FVector::Dist(ViewLocation, Pivot) + MeleeAimRange);
	bool bHit = false;
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params);
		if (!bHit || !Cast<UCapsuleComponent>(Hit.GetComponent())) break;
		Params.AddIgnoredComponent(Hit.GetComponent());
		bHit = false;
	}
	if (bHit && FVector::Dist2D(Hit.ImpactPoint, Character->GetActorLocation()) <= MeleeAimRange)
	{
		bOutOnTarget = true;
		return Hit.ImpactPoint;
	}
	bOutOnTarget = false;
	if (bFirstPerson)
	{
		// Nothing there: the point under the reticle at the punches' own reach from the eyes.
		return ViewLocation + ViewDirection * FVector::Dist(ViewLocation, Natural);
	}
	// Third person looks at the character from behind and above, so with nothing under the reticle only its direction
	// counts: the natural strike, turned to the look direction, at its usual height.
	const float Turn = FRotator::NormalizeAxis(ViewDirection.Rotation().Yaw - (Natural - Pivot).Rotation().Yaw);
	return Pivot + FQuat(FVector::UpVector, FMath::DegreesToRadians(Turn)).RotateVector(Natural - Pivot);
}

void URPGCombatAimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->GetController() || !Character->GetMesh()) return;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const URPGCameraModeComponent* CameraMode = Character->FindComponentByClass<URPGCameraModeComponent>();
	const URPGPrototypeAnimationComponent* Animation = Character->FindComponentByClass<URPGPrototypeAnimationComponent>();
	URPGCombatStateComponent* State = GetCombatState();

	const bool bFirstPerson = CameraMode && CameraMode->bIsFirstPerson;
	const bool bGrounded = Movement && Movement->IsMovingOnGround();
	const bool bMoving = Movement && (Movement->Velocity.Size2D() > 20.f || !Movement->GetCurrentAcceleration().IsNearlyZero());
	const bool bCombat = Animation && Animation->IsInCombat() && bGrounded;
	const bool bAttacking = State && State->AttackPhase != ERPGAttackPhase::None;
	const bool bKick = bAttacking && State->bAttackPhaseIsKick;
	const bool bReferenceLoading=bFirstPerson && Animation && Animation->IsReferencePunchPlaying() && State
		&& (State->AttackPhase==ERPGAttackPhase::Windup || State->AttackPhase==ERPGAttackPhase::ChargeHold);
	// A reference charge already drives the shoulders forward. Tilting that whole
	// body about the eyes adds another forward lunge at close range; let its spine
	// take the aim instead, as in the third-person performance.
	const bool bEyePivot=bFirstPerson && !(Animation && Animation->IsReferenceChargedPunchPlaying());

	// ---- the aim: where the reticle is, measured from the strike's own line ----
	FVector ViewLocation;
	FRotator ViewRotation;
	GetView(Character, ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();
	bool bOnTarget = false;
	FVector StrikePoint=NaturalStrikePoint;
	FVector StrikeFrom=StrikePivot;
	// Each reconstructed fist has its own delivery line. Measure that line for
	// both hands/views, while retaining the established target acquisition range.
	const bool bReferenceStrike=Animation && Animation->IsReferencePunchPlaying() && Animation->GetReferenceStrikePoint(StrikePoint);
	// The filmed punches: where this punch's fist lands with no aim, read from its clip (both hands' midpoint between punches).
	const bool bFilmedStrike=!bReferenceStrike && Animation && Animation->GetFilmedStrikePoint(StrikePoint,StrikeFrom);
	FVector AimPoint = FindAimPoint(Character, ViewLocation, ViewDirection, bFirstPerson, bOnTarget);
	if(bFilmedStrike && !bFirstPerson && !bOnTarget && bAttacking && !bKick)
	{
		// Nothing under the reticle: the shoulder camera's line passes well to the side of the body, so the punch goes
		// to where that line is at the fist's reach (the far point of the view ray on the reach sphere about the pivot,
		// or the ray's closest point when it passes outside it). On screen the fist lands on the reticle. Only while a
		// punch is thrown: between punches the stance keeps facing the look direction.
		const FTransform& MeshToWorld=Character->GetMesh()->GetComponentTransform();
		const FVector Pivot=MeshToWorld.TransformPosition(StrikeFrom);
		const float Reach=FVector::Dist(Pivot,MeshToWorld.TransformPosition(StrikePoint));
		const FVector ToView=ViewLocation-Pivot;
		const float Along=FVector::DotProduct(ViewDirection,ToView);
		const float Inside=Along*Along-(ToView.SizeSquared()-Reach*Reach);
		AimPoint=ViewLocation+ViewDirection*(-Along+FMath::Sqrt(FMath::Max(0.f,Inside)));
	}
	else if((bReferenceStrike || bFilmedStrike) && !bFirstPerson && !bOnTarget)
	{
		const FTransform& MeshToWorld=Character->GetMesh()->GetComponentTransform();
		const FVector Pivot=MeshToWorld.TransformPosition(StrikeFrom),Natural=MeshToWorld.TransformPosition(StrikePoint);
		const float Turn=FRotator::NormalizeAxis(ViewRotation.Yaw-(Natural-Pivot).Rotation().Yaw);
		AimPoint=Pivot+FQuat(FVector::UpVector,FMath::DegreesToRadians(Turn)).RotateVector(Natural-Pivot);
	}
	const FVector ReticlePoint=AimPoint;
	if(bFilmedStrike && !bFirstPerson && bOnTarget && bAttacking && !bKick && State)
	{
		// Third person, a punch at a target: the fist comes in from above and from its own side, and its sweep meets
		// the body before the knuckles reach the reticle point: contact lands high and out toward the punching hand,
		// the more the lower the target (a body shot tilts the strike 20-30 degrees down and grazes the chest /
		// shoulder first). Aim the strike that much low and in; the reticle point itself is unchanged.
		// (A target at the edge of the fist's reach is met at full stretch, not grazed on the way: none there.)
		const FTransform& MeshToWorld=Character->GetMesh()->GetComponentTransform();
		const FVector Pivot=MeshToWorld.TransformPosition(StrikeFrom),Natural=MeshToWorld.TransformPosition(StrikePoint);
		float Inside=FMath::Clamp((FVector::Dist(Pivot,Natural)-FVector::Dist(Pivot,AimPoint))/ContactReachFade,0.f,1.f);
		const float Hand=State->StrikeLimb==ERPGStrikeLimb::RightHand?1.f:State->StrikeLimb==ERPGStrikeLimb::LeftHand?-1.f:0.f;
		const float Depth=FMath::Max(0.f,Natural.Z-AimPoint.Z);
        // Low body shots meet the torso on the downward arc. Retain the filmed in/down compensation there,
        // fading it out above the chest so a chin target keeps its measured knuckle delivery.
        if(Animation->IsCapturedStandardPunchPlaying())Inside*=FMath::Clamp((Depth-8.f)/16.f,0.f,1.f);
		const FVector Right=FRotationMatrix(Character->GetActorRotation()).GetUnitAxis(EAxis::Y);
		AimPoint-=(Right*Hand*(ContactSideOffset+ContactSidePerCm*Depth)+FVector::UpVector*(ContactLiftOffset+ContactLiftPerCm*Depth))*Inside;
	}
	auto MeasureAim = [&](float& OutYaw, float& OutPitch)
	{
		// The camera guard is authored to face the view. Do not aim a held coil
		// using its future impact point: that turns/tilts both loaded fists out
		// of frame. On release the normal strike calibration takes over.
		if(bReferenceLoading)
		{
			OutYaw=FRotator::NormalizeAxis(ViewRotation.Yaw-Character->GetActorRotation().Yaw);
			OutPitch=0.f;return;
		}
		// From the eyes in first person (the chest tilts about them), from the spine in third person.
		const FTransform& MeshToWorld = Character->GetMesh()->GetComponentTransform();
		const FVector From = bEyePivot ? ViewLocation : MeshToWorld.TransformPosition(StrikeFrom);
		const FVector NaturalPoint = MeshToWorld.TransformPosition(StrikePoint);
		const FRotator Natural = (NaturalPoint - From).Rotation();
		const FRotator Wanted = (AimPoint - From).Rotation();
		OutYaw = FRotator::NormalizeAxis(Wanted.Yaw - Natural.Yaw);
		const float Radius=bReferenceStrike || bFilmedStrike?FMath::Max(10.f,float((NaturalPoint-From).Size2D())):ThirdPersonStrikeRadius;
		OutPitch = bFirstPerson ? FRotator::NormalizeAxis(Wanted.Pitch - Natural.Pitch)
			: FMath::RadiansToDegrees(FMath::Atan2(AimPoint.Z - NaturalPoint.Z, Radius));
	};
	float Twist = 0.f, RawPitch = 0.f;
	MeasureAim(Twist, RawPitch);

	// ---- body facing ----
	const float ActorYaw = Character->GetActorRotation().Yaw;
	float Turn = 0.f;
	if (bFirstPerson && (!bCombat || bMoving))
	{
		Turn = FRotator::NormalizeAxis(ViewRotation.Yaw - ActorYaw);   // first person outside a standing fight: face the camera
	}
	else if (bCombat && !bMoving)
	{
		// Past the torso's reach the body turns (speed-limited, so it never snaps); then it squares up a little.
		const float Reach = MaxTorsoYaw * (bKick ? KickTorsoShare : 1.f);
		const float Excess = Twist - FMath::Clamp(Twist, -Reach, Reach);
		const float Speed = (bKick ? KickTurnSpeed : BodyTurnSpeed) * DeltaTime;
		Turn = FMath::Clamp(Excess, -Speed, Speed);
		Turn += (Twist - Turn) * (1.f - FMath::Exp(-SquareUpRate * DeltaTime));
	}
	// (third person moving: the locomotion keeps facing the movement; only the torso aims)
	if (!FMath::IsNearlyZero(Turn, 1e-3f))
	{
		Character->SetActorRotation(FRotator(0.f, ActorYaw + Turn, 0.f));
		MeasureAim(Twist, RawPitch);   // re-measured from the turned body
	}

	// ---- torso aim, within anatomical limits; the reticle may ask for more ----
	const float TargetYaw = FMath::Clamp(Twist, -MaxTorsoYaw, MaxTorsoYaw) * (bKick ? KickTorsoShare : 1.f);
	const float TargetPitch = bKick ? 0.f : FMath::Clamp(RawPitch, -MaxAimPitchDown, MaxAimPitchUp);
	// The body's visible aim follows the raw aim smoothly (the reticle never lags; gameplay data uses the raw aim).
	// (from the wind-up: the filmed punches' 0.16 s load is too short for the calm follow to finish turning)
	const bool bStriking = State && (State->AttackPhase == ERPGAttackPhase::Strike || State->AttackPhase == ERPGAttackPhase::Windup);
	const float Rate = bStriking ? StrikeAimSmoothing : (bFirstPerson ? FirstPersonAimSmoothing : ThirdPersonAimSmoothing);
	const float Follow = Rate > 0.f ? 1.f - FMath::Exp(-Rate * DeltaTime) : 1.f;
	SmoothedYaw += (TargetYaw - SmoothedYaw) * Follow;
	SmoothedPitch += (TargetPitch - SmoothedPitch) * Follow;
	AimAlpha = FMath::FInterpConstantTo(AimAlpha, bCombat ? 1.f : 0.f, DeltaTime, AimBlendSpeed);
	AttackPitchAlpha = FMath::FInterpConstantTo(AttackPitchAlpha, bCombat && bAttacking ? 1.f : 0.f, DeltaTime, AttackPitchBlendSpeed);
	const float AppliedYaw = SmoothedYaw * AimAlpha;
	const float AppliedPitch = SmoothedPitch * AimAlpha * AttackPitchAlpha;

	const FTransform& MeshToWorld = Character->GetMesh()->GetComponentTransform();
	if (URPGAnimInstance* Instance = Cast<URPGAnimInstance>(Character->GetMesh()->GetAnimInstance()))
	{
		Instance->AimYaw = AppliedYaw;
		Instance->AimPitch = AppliedPitch;
		Instance->bPitchAboutPivot = bEyePivot;
		Instance->PitchPivot = MeshToWorld.InverseTransformPosition(ViewLocation);
        Instance->FilmedAim={bFilmedStrike && bAttacking && !bKick,StrikePoint,StrikeFrom,
            MaxTorsoYaw,MaxAimPitchUp,MaxAimPitchDown,AimAlpha,AimAlpha*AttackPitchAlpha};
        const auto* Melee=bFilmedStrike?Character->FindComponentByClass<URPGMeleeHitComponent>():nullptr;
        const bool bLanded=State && Melee && Melee->ConfirmedHitCount>0 && Melee->LastHit.StrikeId==State->StrikeId
            && Melee->LastHitTime>=State->LastReleaseTime;
        if(bFilmedStrike && (bOnTarget || bLanded) && bAttacking && !bKick && State
            && (State->AttackPhase==ERPGAttackPhase::Strike || State->AttackPhase==ERPGAttackPhase::Recovery))
        {
            FVector DeliveryPoint=ReticlePoint;
            if(bLanded)
                DeliveryPoint=Melee->LastHit.AimPoint; // follow through the landing point, never chase a launched target
            // Deliver the leading knuckles to the reticle surface, with their existing half-width outside it.
            // A palm-centre target plus an upward offset made the knuckles graze the upper face first.
            const int32 HandIndex=State->StrikeLimb==ERPGStrikeLimb::LeftHand?RPGStrikeContact::LeftHand:RPGStrikeContact::RightHand;
            const float HalfKnuckle=RPGStrikeContact::Limb(HandIndex).Samples[1].Radius;
            DeliveryPoint-=(DeliveryPoint-ViewLocation).GetSafeNormal()*HalfKnuckle;
            Instance->FilmedAim.DeliveryPoint=MeshToWorld.InverseTransformPosition(DeliveryPoint);
            // The release action can reach this tick before its first strike-time publication. Reading that old
            // zero produces a one-frame full correction and withdrawal. Use the actual montage clock, and enter
            // continuously from zero at the load pose (including a release after holding there).
            float FromLoad=0.f,FromImpact=0.f;
            if(Animation->GetFilmedStrikeTiming(FromLoad,FromImpact))
                Instance->FilmedAim.DeliveryWeight=FMath::SmoothStep(0.f,.04f,FromLoad)*(1.f-FMath::SmoothStep(.02f,.10f,FromImpact));
            Instance->FilmedAim.bLeft=State->StrikeLimb==ERPGStrikeLimb::LeftHand;
        }
	}
	if (State)
	{
		const FVector From = bEyePivot ? ViewLocation : MeshToWorld.TransformPosition(StrikeFrom);
		const FRotator Natural = (MeshToWorld.TransformPosition(StrikePoint) - From).Rotation();
		// Gameplay aim: the raw (clamped) aim, never the smoothed body. The visible body's own turn is published apart.
		State->AttackAimPoint = ReticlePoint;
		State->AttackAimYaw = TargetYaw;
		State->AttackAimPitch = TargetPitch;
		State->AttackAimVisualYaw = AppliedYaw;
		State->AttackAimVisualPitch = AppliedPitch;
		// The strike's direction: its natural line turned by the (clamped) aim.
		State->AttackAimDirection = FRotator(Natural.Pitch + TargetPitch, Natural.Yaw + TargetYaw, 0.f).Vector();
		State->bAttackAimOnTarget = bOnTarget;
		State->bAttackAimInReach = RawPitch <= MaxAimPitchUp + 0.5f && RawPitch >= -MaxAimPitchDown - 0.5f
			&& (FMath::Abs(Twist) <= MaxTorsoYaw + 0.5f || (bCombat && !bMoving));
	}
}
