#include "RPGCombatAimComponent.h"

#include "RPGAnimInstance.h"
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

	// ---- the aim: where the reticle is, measured from the strike's own line ----
	FVector ViewLocation;
	FRotator ViewRotation;
	GetView(Character, ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();
	bool bOnTarget = false;
	const FVector AimPoint = FindAimPoint(Character, ViewLocation, ViewDirection, bFirstPerson, bOnTarget);
	auto MeasureAim = [&](float& OutYaw, float& OutPitch)
	{
		// From the eyes in first person (the chest tilts about them), from the spine in third person.
		const FTransform& MeshToWorld = Character->GetMesh()->GetComponentTransform();
		const FVector From = bFirstPerson ? ViewLocation : MeshToWorld.TransformPosition(StrikePivot);
		const FVector NaturalPoint = MeshToWorld.TransformPosition(NaturalStrikePoint);
		const FRotator Natural = (NaturalPoint - From).Rotation();
		const FRotator Wanted = (AimPoint - From).Rotation();
		OutYaw = FRotator::NormalizeAxis(Wanted.Yaw - Natural.Yaw);
		OutPitch = bFirstPerson ? FRotator::NormalizeAxis(Wanted.Pitch - Natural.Pitch)
			: FMath::RadiansToDegrees(FMath::Atan2(AimPoint.Z - NaturalPoint.Z, ThirdPersonStrikeRadius));
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
	const bool bStriking = State && State->AttackPhase == ERPGAttackPhase::Strike;
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
		Instance->bPitchAboutPivot = bFirstPerson;
		Instance->PitchPivot = MeshToWorld.InverseTransformPosition(ViewLocation);
	}
	if (State)
	{
		const FVector From = bFirstPerson ? ViewLocation : MeshToWorld.TransformPosition(StrikePivot);
		const FRotator Natural = (MeshToWorld.TransformPosition(NaturalStrikePoint) - From).Rotation();
		// Gameplay aim: the raw (clamped) aim, never the smoothed body. The visible body's own turn is published apart.
		State->AttackAimPoint = AimPoint;
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
