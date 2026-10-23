#include "RPGFlightAnimationComponent.h"

#include "RPGCombatStateComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGFlightAnimationComponent::URPGFlightAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Hover = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyHover.RPG_Proto_FlyHover"));
	Forward = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyForward.RPG_Proto_FlyForward"));
	Fast = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FlyFast.RPG_Proto_FlyFast"));
}

void URPGFlightAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	UAnimInstance* Instance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Movement || !Instance) return;

	if (DeltaTime > 0.f)
	{
		const FVector Accel = (Movement->Velocity - FlowLastVelocity) / DeltaTime;
		FlowLastVelocity = Movement->Velocity;
		UpdateLimbFlow(Character->GetMesh(), Instance, Accel, Movement->Velocity, Movement->IsFlying(), DeltaTime);
	}
	if (!Movement->IsFlying())
	{
		ResetBodyReaction(Character->GetMesh());
		if (URPGPhysicsReactionComponent* Physics = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>()) Physics->SetFlightFollow(false);
		if (Playing && Instance->Montage_IsPlaying(Playing)) Instance->Montage_Stop(0.25f, Playing);
		Playing = nullptr;
		PlayingClip = nullptr;
		return;
	}

	UpdateBodyReaction(Character->GetMesh(), Movement->Velocity, DeltaTime);
	// Follow-through: the chest and head lead (the animation and lean above), the rest trails on physics.
	URPGPhysicsReactionComponent* Physics = GetOwner()->FindComponentByClass<URPGPhysicsReactionComponent>();
	if (!Physics && bFollowThrough)
	{
		Physics = NewObject<URPGPhysicsReactionComponent>(GetOwner(), TEXT("RPGFlightPhysics"));
		Physics->bHitReactions = false;
		Physics->RegisterComponent();
	}
	if (Physics) Physics->SetFlightFollow(bFollowThrough);

	// Which pose: fast flight (L3) or high speed -> fast; moving -> forward; otherwise hover.
	const URPGMovementAbilityComponent* Abilities = GetOwner()->FindComponentByClass<URPGMovementAbilityComponent>();
	const float Speed = float(Movement->Velocity.Size2D());
	TSoftObjectPtr<UAnimSequence> Want = Hover;
	const TCHAR* Name = TEXT("RPG_Proto_FlyHover");
	if ((Abilities && Abilities->IsFastFlight() && Speed > ForwardSpeed) || Speed >= FastSpeed) { Want = Fast; Name = TEXT("RPG_Proto_FlyFast"); }
	else if (Speed >= ForwardSpeed) { Want = Forward; Name = TEXT("RPG_Proto_FlyForward"); }
	// The clip for this character's skeleton: the set one if it fits, else the generator's copy for that skeleton
	// (/Game/RPG/Animations/<skeleton>/, e.g. the GASP player's SK_UEFN_Mannequin).
	UAnimSequence* Clip = Want.LoadSynchronous();
	if (!Clip || Clip->GetSkeleton() != Instance->CurrentSkeleton)
	{
		Clip = Instance->CurrentSkeleton ? LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/RPG/Animations/%s/%s.%s"),
			*Instance->CurrentSkeleton->GetName(), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		if (Clip && Clip->GetSkeleton() != Instance->CurrentSkeleton) Clip = nullptr;
	}
	if (!Clip) return;

	// While an attack, charge or block is running it owns the body: wait, then come back. (The combat stance loop is not
	// an attack: flight replaces it.) Otherwise switch pose when it changes.
	const URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
	if (State && (State->AttackPhase != ERPGAttackPhase::None || State->bIsChargingAttack || State->bIsBlocking))
	{
		Playing = nullptr; PlayingClip = nullptr;
		return;
	}
	const bool bOurs = Playing && Instance->Montage_IsPlaying(Playing);
	if (bOurs && PlayingClip == Clip) return;

	Playing = Instance->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), BlendTime, BlendTime, 1.f, 100000);
	PlayingClip = Playing ? Clip : nullptr;
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
		LastVelocity = Velocity;
		LastHeading = Velocity.Rotation().Yaw;
		Pitch = Roll = 0.f;
		SmoothedYaw = float(Owner->GetActorRotation().Yaw);
	}
	// The body turns after the view: a smoothed yaw that catches up with the actor's.
	const float ActorYaw = float(Owner->GetActorRotation().Yaw);
	const float YawStep = FMath::FindDeltaAngleDegrees(SmoothedYaw, ActorYaw) * (1.f - FMath::Exp(-TurnFollowSpeed * DeltaTime));
	SmoothedYaw = FRotator::NormalizeAxis(SmoothedYaw + YawStep);
	TurnRateRad = FMath::DegreesToRadians(YawStep / DeltaTime);
	const float YawLag = FMath::FindDeltaAngleDegrees(ActorYaw, SmoothedYaw);
	const float Horizontal = float(Velocity.Size2D());
	const float React = FMath::Lerp(HoverReaction, 1.f, FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f));
	const FVector Accel = (Velocity - LastVelocity) / DeltaTime;
	const float AccelForward = float(FVector::DotProduct(Accel, Owner->GetActorForwardVector()));
	const float Side = float(FVector::DotProduct(Accel, Owner->GetActorRightVector()));
	// Climb / dive along the flight path, plus leaning into speeding up (nose down) and back when slowing.
	float WantPitch = FMath::RadiansToDegrees(FMath::Atan2(float(Velocity.Z), FMath::Max(Horizontal, 300.f))) * ClimbPitch;
	WantPitch -= FMath::Clamp(AccelForward / 1000.f * AccelLean, -15.f, 15.f);
	WantPitch = FMath::Clamp(WantPitch, -MaxPitch, MaxPitch);
	// Bank into turns (how fast the travel direction swings, more at speed) and lean into sideways pushes.
	const float Heading = Horizontal > 50.f ? float(Velocity.Rotation().Yaw) : LastHeading;
	const float TurnRate = FMath::FindDeltaAngleDegrees(LastHeading, Heading) / DeltaTime;
	float WantRoll = TurnRate * BankPerTurn * FMath::Clamp(Horizontal / FMath::Max(FastSpeed, 1.f), 0.f, 1.f) + FMath::Clamp(Side / 1000.f * AccelLean, -15.f, 15.f);
	WantRoll = FMath::Clamp(WantRoll, -MaxBank, MaxBank);
	Pitch = FMath::FInterpTo(Pitch, WantPitch * React, DeltaTime, ReactionSpeed);
	Roll = FMath::FInterpTo(Roll, WantRoll * React, DeltaTime, ReactionSpeed);
	LastVelocity = Velocity;
	LastHeading = Heading;

	// Turn about the hips, in the actor's frame.
	const FQuat Lean = FQuat(FVector::UpVector, FMath::DegreesToRadians(YawLag)) * FRotator(Pitch, 0.f, Roll).Quaternion();
	const FVector Pivot = RestLocation + FVector(0.f, 0.f, PivotHeight);
	Mesh->SetRelativeLocationAndRotation(Pivot + Lean.RotateVector(RestLocation - Pivot), Lean * RestRotation);
}

void URPGFlightAnimationComponent::ResetBodyReaction(USkeletalMeshComponent* Mesh)
{
	if (!bReacting) return;
	bReacting = false;
	TurnRateRad = 0.f;
	if (Mesh) Mesh->SetRelativeLocationAndRotation(RestLocation, RestRotation);
}

namespace
{
	/** Follow-through joints, parents first: bone, the next bone down the limb (its direction is read from the pose each
	 *  frame, so hanging, along-the-body and horizontal poses all swing the right way), how much momentum moves it, how
	 *  stiff it is (lower = lags more, so the motion rolls down the chain), how much the air drags it, which side it is
	 *  on (+1 left, -1 right: it swings outward freely but only a little inward, so legs don't cross and hands don't go
	 *  into the body), and whether it is an upper arm / thigh (where the steering acts). Arms are held actively: about
	 *  half the swing, stiffer, no drag. */
	struct FFlowJoint { const TCHAR* Bone; const TCHAR* Child; float Gain; float Stiff; float Drag; float Side; bool bArm; bool bThigh; };
	const FFlowJoint FlowJoints[] = {
		{ TEXT("spine_01"), TEXT("spine_02"), 0.12f, 2.f, 1.f, 0.f, false, false },
		{ TEXT("spine_02"), TEXT("spine_03"), 0.08f, 2.f, 1.f, 0.f, false, false },
		{ TEXT("thigh_l"), TEXT("calf_l"), 1.f, 1.f, 1.f, 1.f, false, true },
		{ TEXT("calf_l"), TEXT("foot_l"), 0.8f, 0.6f, 1.f, 1.f, false, false },
		{ TEXT("thigh_r"), TEXT("calf_r"), 1.f, 1.f, 1.f, -1.f, false, true },
		{ TEXT("calf_r"), TEXT("foot_r"), 0.8f, 0.6f, 1.f, -1.f, false, false },
		{ TEXT("upperarm_l"), TEXT("lowerarm_l"), 0.45f, 1.5f, 0.f, 1.f, true, false },
		{ TEXT("lowerarm_l"), TEXT("hand_l"), 0.3f, 1.2f, 0.f, 1.f, false, false },
		{ TEXT("upperarm_r"), TEXT("lowerarm_r"), 0.45f, 1.5f, 0.f, -1.f, true, false },
		{ TEXT("lowerarm_r"), TEXT("hand_r"), 0.3f, 1.2f, 0.f, -1.f, false, false },
	};
}

void URPGFlightAnimationComponent::UpdateLimbFlow(USkeletalMeshComponent* Mesh, UAnimInstance* Instance, const FVector& Accel, const FVector& Velocity, bool bFlying, float DeltaTime)
{
	URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Instance);
	if (!Mesh || !Anim) return;
	const int32 N = UE_ARRAY_COUNT(FlowJoints);
	if (FlowAngle.Num() != N) { FlowAngle.Init(FVector::ZeroVector, N); FlowSpeed.Init(FVector::ZeroVector, N); FlowDir.Init(FVector::ZeroVector, N); }
	FlowWeight = FMath::FInterpConstantTo(FlowWeight, bFlying && bLimbFlow ? 1.f : 0.f, DeltaTime, 3.f);
	if (FlowWeight <= 0.f)
	{
		for (int32 I = 0; I < N; ++I) FlowAngle[I] = FlowSpeed[I] = FVector::ZeroVector;
		Anim->FlowBones.Reset();
		Anim->FlowRotations.Reset();
		return;
	}
	// Momentum in the mesh's own frame (+Y forward, +X the character's left, +Z up): limbs swing against the
	// acceleration and are swept back by the air.
	const FTransform& ToMesh = Mesh->GetComponentTransform();
	const FVector A = ToMesh.InverseTransformVectorNoScale(Accel.GetClampedToMaxSize(6000.f)) / 1000.f;
	const FVector V = ToMesh.InverseTransformVectorNoScale(Velocity) / 1000.f;
	const float SpeedAlpha = FMath::Clamp(float(Velocity.Size()) / FMath::Max(FastSpeed, 1.f), 0.f, 1.f);
	const float MaxSwing = FMath::DegreesToRadians(FlowMaxAngle);
	const float Brake = FMath::Clamp(-A.Y * 1000.f / FlapBrake, 0.f, 1.f);   // slowing along the body's forward (+Y)
	const float PathPitch = FMath::RadiansToDegrees(FMath::Atan2(float(Velocity.Z), FMath::Max(float(Velocity.Size2D()), 300.f)));
	// Bracing per side (degrees out from the body), from the turn's sideways force (smoothed): turning right pulls the
	// body to its right (-X), so the left arm is the outer one. The inner arm tucks a little; both spread braking (flaps).
	SideForce = FMath::FInterpTo(SideForce, float(A.X), DeltaTime, 6.f);
	const float LeftOuter = FMath::Max(0.f, -SideForce), RightOuter = FMath::Max(0.f, SideForce);
	auto BraceFor = [&](float Outer, float Inner)
	{
		return FMath::Min(FlowBrace * Outer + FlowBraceSharp * Outer * Outer, FlowBraceMax)
			- FMath::Min(0.35f * FlowBrace * Inner, 0.35f * FlowBraceMax) + FlapArms * Brake;
	};
	// Wingsuit spread (0..1): both sides open in fast, hard corners, fading with speed.
	const float Spread = FMath::Clamp(FMath::Abs(SideForce) / WingsuitForce * SpeedAlpha, 0.f, 1.f);
	const float BraceL = FMath::Min(BraceFor(LeftOuter, RightOuter) + WingsuitArms * Spread, FlowBraceMax);
	const float BraceR = FMath::Min(BraceFor(RightOuter, LeftOuter) + WingsuitArms * Spread, FlowBraceMax);
	// Small steps so the springs stay stable at any frame rate.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / (1.f / 120.f)), 1, 8);
	const float Dt = DeltaTime / Steps;
	Anim->FlowBones.SetNum(N);
	Anim->FlowRotations.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		const FFlowJoint& J = FlowJoints[I];
		// The limb's current direction, and the turns that swing its tip outward (away from the body's middle) and
		// forward: a turn W moves the tip along W x Dir, so the axis for a direction X is Dir x X.
		// (Read from last frame's pose with this joint's own swing taken back out, and smoothed, so a big swing doesn't
		// feed back into itself.)
		FVector Dir = Mesh->GetBoneLocation(FName(J.Child), EBoneSpaces::ComponentSpace) - Mesh->GetBoneLocation(FName(J.Bone), EBoneSpaces::ComponentSpace);
		const FVector Applied = I < Anim->FlowRotations.Num() ? Anim->FlowRotations[I] : FVector::ZeroVector;
		if (Applied.Size() > 1.e-4f) Dir = FQuat(Applied.GetSafeNormal(), float(Applied.Size())).Inverse().RotateVector(Dir);
		Dir = Dir.IsNearlyZero() ? FVector(0, 0, -1) : Dir.GetSafeNormal();
		FlowDir[I] = FlowDir[I].IsNearlyZero() ? Dir : (FlowDir[I] + (Dir - FlowDir[I]) * FMath::Min(1.f, 6.f * DeltaTime)).GetSafeNormal();
		Dir = FlowDir[I];
		const FVector OutAxis = J.Side != 0.f ? FVector::CrossProduct(Dir, FVector(J.Side, 0, 0)).GetSafeNormal() : FVector::ZeroVector;
		const FVector ForwardAxis = FVector::CrossProduct(Dir, FVector(0, 1, 0)).GetSafeNormal();

		// Steering rest (where the spring pulls), like a plane's control surfaces.
		FVector Rest = FVector::ZeroVector;
		float Stiffen = 1.f;
		const float Brace = J.Side > 0.f ? BraceL : BraceR;
		if (J.bArm)
		{
			Rest = OutAxis * FMath::DegreesToRadians(Brace);
			Stiffen += BraceStiffen * FMath::Clamp(Brace / FMath::Max(FlowBraceMax, 1.f), 0.f, 1.f);   // a braced arm is held firm
		}
		else if (J.Side != 0.f && !J.bThigh && FCString::Strstr(J.Bone, TEXT("lowerarm")))
			Stiffen += BraceStiffen * FMath::Clamp(Brace / FMath::Max(FlowBraceMax, 1.f), 0.f, 1.f);   // and kept straight
		if (J.bThigh)
		{
			// Rudder: both legs to the outside of the turn (turning left -> to the right); elevators: forward and under
			// climbing, trailing back diving; flaps: open a little when braking.
			const float Rudder = FMath::Clamp(RudderPerTurn * TurnRateRad, -RudderMax, RudderMax);   // + = toward the left
			const FVector LeftAxis = FVector::CrossProduct(Dir, FVector(1, 0, 0)).GetSafeNormal();
			Rest = LeftAxis * FMath::DegreesToRadians(Rudder) + OutAxis * FMath::DegreesToRadians(FlapLegs * Brake + WingsuitLegs * Spread)
				+ ForwardAxis * FMath::DegreesToRadians(FMath::Clamp(ElevatorPerDegree * PathPitch, -ElevatorMax, ElevatorMax));
		}

		const float K = FlowStiffness * J.Stiff * Stiffen * FMath::Lerp(1.f, FlowStiffnessAtSpeed, SpeedAlpha);
		const float C = 2.f * FlowDamping * FMath::Sqrt(K);
		const FVector Push = J.Gain / Stiffen * (FlowInertia * FVector::CrossProduct(Dir, -A) + FlowDrag * J.Drag * SpeedAlpha * FVector::CrossProduct(Dir, -V)
			- FlowTurn * TurnRateRad * FVector::UpVector * 10.f);   // lag behind the body's turn
		for (int32 S = 0; S < Steps; ++S)
		{
			FlowSpeed[I] += (Push - K * (FlowAngle[I] - Rest) - C * FlowSpeed[I]) * Dt;
			FlowAngle[I] += FlowSpeed[I] * Dt;
		}
		// Swing off the steering rest is limited; the rest itself may go further (a full brace).
		FVector Off = FlowAngle[I] - Rest;
		if (Off.Size() > MaxSwing) { Off = Off.GetSafeNormal() * MaxSwing; FlowAngle[I] = Rest + Off; FlowSpeed[I] *= 0.5f; }
		if (J.Side != 0.f)
		{
			// Inward (toward the body's middle / the other limb) only a little past the rest.
			const float Inward = -float(FVector::DotProduct(Off, OutAxis));
			const float Limit = 0.06f;
			if (Inward > Limit)
			{
				FlowAngle[I] += OutAxis * (Inward - Limit);
				const float Vin = -float(FVector::DotProduct(FlowSpeed[I], OutAxis));
				if (Vin > 0.f) FlowSpeed[I] += OutAxis * Vin;
			}
		}
		Anim->FlowBones[I] = FName(J.Bone);
		Anim->FlowRotations[I] = FlowAngle[I] * FlowWeight;
	}
}
