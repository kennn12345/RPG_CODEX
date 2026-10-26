#include "RPGHitReactionComponent.h"

#include "RPGAnimInstance.h"
#include "RPG_CODEX.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	/** Reacting joints, parents first: bone, anatomical limit (degrees), stiffness factor, share of the torque it takes
	 *  when several joints of the same run (the spine) share one hit, whether it is an upper arm. Knees and elbows are
	 *  left to the animation (no hinge bending the wrong way); the arms move from the shoulder. */
	struct FJointDef { const TCHAR* Bone; float LimitDeg; float Stiff; float Share; bool bArm; };
	const FJointDef JointDefs[] = {
		{ TEXT("spine_01"), 7.f, 1.6f, 0.35f, false },
		{ TEXT("spine_02"), 7.f, 1.6f, 0.35f, false },
		{ TEXT("spine_03"), 8.f, 1.5f, 0.35f, false },
		{ TEXT("spine_04"), 8.f, 1.5f, 0.35f, false },
		{ TEXT("spine_05"), 9.f, 1.4f, 0.35f, false },
		{ TEXT("neck_01"), 14.f, 1.2f, 0.6f, false },
		{ TEXT("head"), 22.f, 1.f, 1.f, false },
		{ TEXT("clavicle_l"), 10.f, 1.5f, 1.f, false },
		{ TEXT("upperarm_l"), 40.f, 0.8f, 1.f, true },
		{ TEXT("clavicle_r"), 10.f, 1.5f, 1.f, false },
		{ TEXT("upperarm_r"), 40.f, 0.8f, 1.f, true },
		{ TEXT("thigh_l"), 10.f, 2.f, 1.f, false },
		{ TEXT("thigh_r"), 10.f, 2.f, 1.f, false },
	};
}

URPGHitReactionComponent::URPGHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

USkeletalMeshComponent* URPGHitReactionComponent::GetMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner())) return Character->GetMesh();
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void URPGHitReactionComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(false);   // only while a reaction is playing out
}

void URPGHitReactionComponent::SetUpJoints(USkeletalMeshComponent* Mesh)
{
	Joints.Reset();
	for (const FJointDef& Def : JointDefs)
	{
		if (Mesh->GetBoneIndex(Def.Bone) == INDEX_NONE) continue;
		FJoint J;
		J.Bone = Def.Bone;
		J.Limit = FMath::DegreesToRadians(Def.LimitDeg);
		J.Stiff = Def.Stiff;
		J.bArm = Def.bArm;
		// What this joint has to turn: every body at or below it in the skeleton (realistic weights, RPGBodyMass).
		float Mass = 0.f;
		for (const FBodyInstance* Body : Mesh->Bodies)
		{
			if (!Body) continue;
			const FName BodyBone = Mesh->GetBoneName(Body->InstanceBoneIndex);
			if (BodyBone == J.Bone || Mesh->BoneIsChildOf(BodyBone, J.Bone)) Mass += Body->GetBodyMass();
		}
		J.MovedMass = FMath::Max(Mass, 0.5f);
		Joints.Add(J);
	}
	// Each joint's nearest reacting ancestor.
	for (int32 I = 0; I < Joints.Num(); ++I)
		for (int32 P = I - 1; P >= 0; --P)
			if (Mesh->BoneIsChildOf(Joints[I].Bone, Joints[P].Bone)) { Joints[I].Parent = P; break; }
}

int32 URPGHitReactionComponent::FindJoint(FName Bone, const USkeletalMeshComponent* Mesh) const
{
	// The struck bone, or the nearest reacting joint above it (a hand hit turns the shoulder, a shin hit the hip).
	for (FName B = Bone; B != NAME_None; B = Mesh->GetParentBone(B))
		for (int32 I = 0; I < Joints.Num(); ++I)
			if (Joints[I].Bone == B) return I;
	return INDEX_NONE;
}

void URPGHitReactionComponent::AddHit(FName Bone, const FVector& Point, const FVector& Impulse)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || Impulse.IsNearlyZero()) return;
	if (Joints.Num() == 0) SetUpJoints(Mesh);
	int32 Index = FindJoint(Bone, Mesh);
	if (Index == INDEX_NONE) Index = FindJoint(TEXT("spine_03"), Mesh);
	if (Index == INDEX_NONE) return;

	const FTransform& ToComponent = Mesh->GetComponentTransform();
	const FVector P = ToComponent.InverseTransformPosition(Point);
	const FVector J = ToComponent.InverseTransformVectorNoScale(Impulse);
	// Every joint from the struck one up to the hips: the hit's torque about that joint over what it has to turn.
	FVector ChestKick = FVector::ZeroVector;
	for (int32 I = Index; I != INDEX_NONE; I = Joints[I].Parent)
	{
		FJoint& Joint = Joints[I];
		const FVector Pivot = Mesh->GetBoneLocation(Joint.Bone, EBoneSpaces::ComponentSpace);
		const FVector R = P - Pivot;
		const float Inertia = Joint.MovedMass * (float(R.SizeSquared()) + 100.f);   // kg cm^2 (a point mass at the hit; +100 keeps it finite)
		float JointShare = 1.f;
		for (const FJointDef& Def : JointDefs) if (Joint.Bone == FName(Def.Bone)) { JointShare = Def.Share; break; }
		const FVector Kick = FVector::CrossProduct(R, J) / Inertia * Responsiveness * JointShare;   // rad/s
		Joint.Speed += Kick;
		if (Joint.Bone == TEXT("spine_05") || Joint.Bone == TEXT("spine_04")) ChestKick += Kick;
	}
	// The arms trail the torso's jolt: a loose arm stays put a moment while the chest turns under it.
	for (FJoint& Joint : Joints)
		if (Joint.bArm) Joint.Speed -= ChestKick * ArmTrail;
	bActive = true;
	SetComponentTickEnabled(true);
}

void URPGHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USkeletalMeshComponent* Mesh = GetMesh();
	URPGAnimInstance* Anim = Mesh ? Cast<URPGAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Anim || DeltaTime <= 0.f) return;

	// Damped springs back to the pose, in small steps so they stay smooth at any frame rate.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime * 120.f), 1, 8);
	const float Dt = DeltaTime / Steps;
	const float Cap = FMath::DegreesToRadians(MaxAngle);
	bool bMoving = false;
	Anim->FlowBones.SetNum(Joints.Num());
	Anim->FlowRotations.SetNum(Joints.Num());
	for (int32 I = 0; I < Joints.Num(); ++I)
	{
		FJoint& J = Joints[I];
		const float K = Stiffness * J.Stiff;
		const float C = 2.f * Damping * FMath::Sqrt(K);
		for (int32 S = 0; S < Steps; ++S)
		{
			J.Speed += (-K * J.Angle - C * J.Speed) * Dt;
			J.Angle += J.Speed * Dt;
		}
		// Anatomical limit: a joint only turns so far; at the stop it loses the speed pushing past it.
		const float Limit = FMath::Min(J.Limit, Cap);
		const float Size = float(J.Angle.Size());
		if (Size > Limit)
		{
			const FVector Axis = J.Angle / Size;
			J.Angle = Axis * Limit;
			const float Out = float(FVector::DotProduct(J.Speed, Axis));
			if (Out > 0.f) J.Speed -= Axis * Out;
		}
		if (Size > 1.e-4f || J.Speed.SizeSquared() > 1.e-6f) bMoving = true;
		Anim->FlowBones[I] = J.Bone;
		Anim->FlowRotations[I] = J.Angle;
	}
	if (!bMoving)
	{
		for (FJoint& J : Joints) J.Angle = J.Speed = FVector::ZeroVector;
		Anim->FlowBones.Reset();
		Anim->FlowRotations.Reset();
		bActive = false;
		SetComponentTickEnabled(false);
	}
}
