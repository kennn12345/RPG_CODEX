#include "RPGStrikeContactComponent.h"

#include "RPGAnimInstance.h"
#include "RPGCombatStateComponent.h"
#include "RPGMeleeTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

using namespace RPGStrikeContact;

namespace
{
	TAutoConsoleVariable<int32> CVarStrikeContact(
		TEXT("rpg.StrikeContact"), 1,
		TEXT("Strike contact: fists and feet stop at the surface of what they hit (1, default) or pass through as animated (0)."));

	TAutoConsoleVariable<int32> CVarStrikeContactDebug(
		TEXT("rpg.StrikeContactDebug"), 0,
		TEXT("Draw strike contact planes and the animated (red) / corrected (green) limb points."));

	/** Largest jump a surface is predicted ahead by in one frame (a yielding body moves a few cm a frame at most). */
	constexpr float MaxPlaneLead = 6.f;

	FVector SamplePoint(const USkeletalMeshComponent* Mesh, const FSurfaceSample& Sample)
	{
		return FMath::Lerp(Mesh->GetBoneLocation(Sample.A), Mesh->GetBoneLocation(Sample.B), Sample.T);
	}

	/** The bodies of a target's primitive, each with the bone it belongs to. */
	void TargetBodies(UPrimitiveComponent* Target, TArray<TPair<FBodyInstance*, FName>>& Out)
	{
		Out.Reset();
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Target))
		{
			for (FBodyInstance* Body : Skeletal->Bodies)
				if (Body && Body->IsValidBodyInstance() && Body->BodySetup.IsValid()) Out.Emplace(Body, Body->BodySetup->BoneName);
		}
		else if (FBodyInstance* Body = Target->GetBodyInstance(); Body && Body->IsValidBodyInstance())
		{
			Out.Emplace(Body, NAME_None);
		}
	}
}

URPGStrikeContactComponent::URPGStrikeContactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After this frame's pose and the targets' physics: the contact found here shapes the next pose.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

USkeletalMeshComponent* URPGStrikeContactComponent::GetMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

bool URPGStrikeContactComponent::IsActive() const
{
	return bEnabled && CVarStrikeContact.GetValueOnGameThread() != 0;
}

bool URPGStrikeContactComponent::IsAttacking(float DeltaTime)
{
	const URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
	const bool bNow = State && (State->AttackPhase != ERPGAttackPhase::None || State->bStrikeHitWindowOpen);
	SinceAttack = bNow ? 0.f : SinceAttack + DeltaTime;
	return SinceAttack <= AttackEndGrace;
}

FVector URPGStrikeContactComponent::GetCorrection(ERPGStrikeLimb Limb) const
{
	const int32 Index = IndexOf(Limb);
	return Index == INDEX_NONE ? FVector::ZeroVector : Limbs[Index].Correction;
}

bool URPGStrikeContactComponent::IsInContact(ERPGStrikeLimb Limb) const
{
	const int32 Index = IndexOf(Limb);
	return Index != INDEX_NONE && Limbs[Index].Latches.Num() > 0;
}

bool URPGStrikeContactComponent::HasStrikingContact(ERPGStrikeLimb Limb) const
{
	const int32 Index = IndexOf(Limb);
	return Index != INDEX_NONE && Limbs[Index].Latches.Num() > 0 && Limbs[Index].ApproachSpeed >= MinStrikeSpeed;
}

FVector URPGStrikeContactComponent::GetAnimatedTravel(ERPGStrikeLimb Limb) const
{
	const int32 Index = IndexOf(Limb);
	if (Index == INDEX_NONE || !Limbs[Index].bHaveLast) return FVector::ZeroVector;
	return Limbs[Index].AnimatedBone - Limbs[Index].LastAnimatedBone;
}

bool URPGStrikeContactComponent::GetContact(ERPGStrikeLimb Limb, UPrimitiveComponent*& OutTarget, FName& OutBone, FVector& OutPoint, FVector& OutNormal) const
{
	const int32 Index = IndexOf(Limb);
	if (Index == INDEX_NONE || Limbs[Index].Latches.Num() == 0 || Limbs[Index].Set.Num == 0) return false;
	const FLatch& Latch = Limbs[Index].Latches[0];
	OutTarget = Latch.Target.Get();
	OutBone = Latch.Bone;
	OutPoint = Latch.LastPoint;
	OutNormal = Limbs[Index].Set.Planes[0].Normal;
	return OutTarget != nullptr;
}

void URPGStrikeContactComponent::GetJointAngles(ERPGStrikeLimb Limb, float& OutAnimated, float& OutCorrected) const
{
	const int32 Index = IndexOf(Limb);
	OutAnimated = Index == INDEX_NONE ? 180.f : Limbs[Index].AnimatedAngle;
	OutCorrected = Index == INDEX_NONE ? 180.f : Limbs[Index].CorrectedAngle;
}

void URPGStrikeContactComponent::GatherTargets(TArray<UPrimitiveComponent*>& Out) const
{
	Out.Reset();
	const UWorld* World = GetWorld();
	if (!World || !GetOwner()) return;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGStrikeContact), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, GetOwner()->GetActorLocation(), FQuat::Identity, Objects, FCollisionShape::MakeSphere(SearchRadius), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		const AActor* Actor = Overlap.GetActor();
		// Only things that take melee hits, and their skeletal bodies (not a blocking capsule).
		if (Component && Actor && Actor != GetOwner() && Actor->Implements<URPGMeleeTarget>() && Component->IsA<USkeletalMeshComponent>())
			Out.AddUnique(Component);
	}
}

bool URPGStrikeContactComponent::BodyTransform(const UPrimitiveComponent* Target, FName Bone, FTransform& Out) const
{
	if (!Target) return false;
	if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Target); Skeletal && Bone != NAME_None)
	{
		// The body itself when it simulates (a reaction), otherwise the bone it follows.
		if (const FBodyInstance* Body = Skeletal->GetBodyInstance(Bone); Body && Body->IsInstanceSimulatingPhysics())
		{
			Out = Body->GetUnrealWorldTransform();
			return true;
		}
		const int32 Index = Skeletal->GetBoneIndex(Bone);
		if (Index == INDEX_NONE) return false;
		Out = Skeletal->GetBoneTransform(Index);
		return true;
	}
	Out = Target->GetComponentTransform();
	return true;
}

void URPGStrikeContactComponent::UpdateLimb(int32 Index, const TArray<UPrimitiveComponent*>& Targets, float DeltaTime)
{
	FLimbState& S = Limbs[Index];
	const FLimbDef& Def = Limb(Index);
	const USkeletalMeshComponent* Mesh = GetMesh();

	// The end segment's spheres now (as drawn) and where the animation alone will be heading; the elbow / knee and
	// forearm / shin now and ahead at the limb's speed. Two frames ahead: strikes accelerate into the target, and a
	// one-frame, constant-speed guess falls short of the surface the frame before contact (a plane further on only
	// matters once the limb gets there).
	constexpr int32 NumSamples = 5;
	const FSurfaceSample Samples[NumSamples] = {Def.Samples[0], Def.Samples[1], Def.Samples[2], Def.Inner[0], Def.Inner[1]};
	FVector Now[NumSamples], Next[NumSamples];
	const FVector Velocity = S.bHaveLast ? S.Animated - S.LastAnimated : FVector::ZeroVector;
	for (int32 K = 0; K < NumSamples; ++K)
	{
		Now[K] = SamplePoint(Mesh, Samples[K]);
		Next[K] = Now[K] - (K < 3 ? S.Correction : FVector::ZeroVector) + Velocity * 2.f;
	}

	// Contacts already held: each moves with its body; it lets go once the animated limb is back out in front of it.
	for (int32 L = S.Latches.Num() - 1; L >= 0; --L)
	{
		const FLatch& Latch = S.Latches[L];
		FTransform BT;
		bool bKeep = Latch.Target.IsValid() && BodyTransform(Latch.Target.Get(), Latch.Bone, BT);
		if (bKeep)
		{
			const FVector P = BT.TransformPosition(Latch.LocalPoint);
			const FVector N = BT.TransformVectorNoScale(Latch.LocalNormal).GetSafeNormal();
			float Clearance = TNumericLimits<float>::Max();
			for (int32 K = 0; K < NumSamples; ++K)
				Clearance = FMath::Min(Clearance, float(FVector::DotProduct(Now[K] - (K < 3 ? S.Correction : FVector::ZeroVector) + Velocity - P, N)) - Samples[K].Radius);
			bKeep = Clearance <= ReleaseDistance
				&& FVector::Dist(P, Latch.StartPoint) <= MaxContactDrift
				&& FVector::DotProduct(N, Latch.StartNormal) >= FMath::Cos(FMath::DegreesToRadians(MaxContactTurn));
		}
		if (!bKeep) S.Latches.RemoveAt(L);
	}

	// Held contacts follow the curve of their body: the surface is re-taken each frame at the closest point to the sphere
	// that touched it (a capsule is round; one tangent plane would let a fist sliding over it sink in), as long as that
	// sphere is outside the body (inside, the held plane stays).
	for (FLatch& Latch : S.Latches)
	{
		const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Latch.Target.Get());
		const FBodyInstance* Body = Skeletal ? Skeletal->GetBodyInstance(Latch.Bone) : nullptr;
		FTransform BT;
		float DistSq = 0.f;
		FVector Closest;
		if (!Body || !BodyTransform(Latch.Target.Get(), Latch.Bone, BT) || !Body->GetSquaredDistanceToBody(Now[Latch.Sample], DistSq, Closest) || DistSq < 0.01f) continue;
		const FVector Normal = (Now[Latch.Sample] - Closest).GetSafeNormal();
		if (FVector::DotProduct(Normal, BT.TransformVectorNoScale(Latch.LocalNormal)) < 0.5f) continue;   // round a corner: keep the held one
		Latch.LocalPoint = BT.InverseTransformPosition(Closest);
		Latch.LocalNormal = BT.InverseTransformVectorNoScale(Normal);
	}

	// New contacts along the path: per body, the earliest sweep hit of any sphere.
	TArray<TPair<FBodyInstance*, FName>> Bodies;
	for (UPrimitiveComponent* Target : Targets)
	{
		TargetBodies(Target, Bodies);
		for (const TPair<FBodyInstance*, FName>& Body : Bodies)
		{
			if (S.Latches.Num() >= MaxPlanes) break;
			if (S.Latches.ContainsByPredicate([&](const FLatch& L) { return L.Target.Get() == Target && L.Bone == Body.Value; })) continue;
			float BestTime = 2.f;
			int32 BestSample = 0;
			FVector BestPoint = FVector::ZeroVector, BestNormal = FVector::UpVector;
			for (int32 K = 0; K < NumSamples; ++K)
			{
				const FCollisionShape Sphere = FCollisionShape::MakeSphere(Samples[K].Radius);
				FHitResult Hit;
				if (FVector::DistSquared(Now[K], Next[K]) > 0.01f && Body.Key->Sweep(Hit, Now[K], Next[K], FQuat::Identity, Sphere, false))
				{
					if (!Hit.bStartPenetrating && Hit.Time < BestTime)
					{
						BestTime = Hit.Time;
						BestSample = K;
						BestPoint = Hit.ImpactPoint;
						BestNormal = Hit.Normal;
					}
					if (!Hit.bStartPenetrating) continue;
				}
				// Not moving, or already in it (crept in sideways while another surface held it): the way out of the overlap,
				// if it is not so deep that the nearest way out could be the far side.
				FMTDResult MTD;
				if (BestTime > 1.f && Body.Key->OverlapTest(Now[K], FQuat::Identity, Sphere, &MTD) && MTD.Distance < Samples[K].Radius + 6.f && !MTD.Direction.IsNearlyZero())
				{
					BestTime = 1.f;
					BestSample = K;
					BestNormal = MTD.Direction.GetSafeNormal();
					BestPoint = Now[K] + BestNormal * (MTD.Distance - Samples[K].Radius);
				}
			}
			FTransform BT;
			if (BestTime > 1.f || !BodyTransform(Target, Body.Value, BT)) continue;
			FLatch& Latch = S.Latches.AddDefaulted_GetRef();
			Latch.Target = Target;
			Latch.Bone = Body.Value;
			Latch.LocalPoint = BT.InverseTransformPosition(BestPoint);
			Latch.LocalNormal = BT.InverseTransformVectorNoScale(BestNormal.GetSafeNormal());
			Latch.Sample = BestSample;
			Latch.StartPoint = BestPoint;
			Latch.StartNormal = BestNormal.GetSafeNormal();
		}
	}

	// The planes for the next pose: where each surface will be by then (one more frame of its own motion, so the limb
	// neither lags behind a yielding / walking target nor sinks into one coming at it).
	S.Set.Num = 0;
	S.ApproachSpeed = 0.f;
	for (FLatch& Latch : S.Latches)
	{
		FTransform BT;
		if (!BodyTransform(Latch.Target.Get(), Latch.Bone, BT)) continue;
		const FVector P = BT.TransformPosition(Latch.LocalPoint);
		const FVector N = BT.TransformVectorNoScale(Latch.LocalNormal).GetSafeNormal();
		const FVector SurfaceVelocity = Latch.bHaveLast ? (P - Latch.LastPoint) / DeltaTime : FVector::ZeroVector;
		Latch.LastPoint = P;
		Latch.bHaveLast = true;
		FRPGLimbContactPlane& Plane = S.Set.Planes[S.Set.Num++];
		Plane.Point = P + (SurfaceVelocity * DeltaTime).GetClampedToMaxSize(MaxPlaneLead);
		Plane.Normal = N;
		const float Into = float(FVector::DotProduct(SurfaceVelocity - Velocity / DeltaTime, N));
		S.ApproachSpeed = FMath::Max(S.ApproachSpeed, Into);
		// Still driving in: the target is pushed (it can give way so the strike follows through).
		AActor* TargetActor = Latch.Target.IsValid() ? Latch.Target->GetOwner() : nullptr;
		if (Into > 0.f && TargetActor && TargetActor->Implements<URPGMeleeTarget>())
			IRPGMeleeTarget::Execute_ReceiveContactPush(TargetActor, -N * Into, Latch.Bone);
	}
}

void URPGStrikeContactComponent::PushPlanes()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;
	TArray<USkeletalMeshComponent*> Meshes;
	Character->GetComponents<USkeletalMeshComponent>(Meshes);
	// The visible body and the first-person shadow body (same surfaces, so the shadow's limbs stop in the same place).
	for (USkeletalMeshComponent* Mesh : Meshes)
		if (URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Mesh->GetAnimInstance()))
		{
			for (int32 I = 0; I < NumLimbs; ++I) Anim->ContactSets[I] = Limbs[I].Set;
			Anim->ContactReleaseTime = ReleaseTime;
		}
}

void URPGStrikeContactComponent::PushBack(float DeltaTime)
{
	// What the limbs could not take: the attacker backs off (horizontally, swept, a little each frame).
	FVector Residual = FVector::ZeroVector;
	for (const FLimbState& S : Limbs)
		if (S.Residual.SizeSquared() > Residual.SizeSquared()) Residual = S.Residual;
	Residual.Z = 0.f;
	const URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
	if (State && State->StrikeId != PushbackStrikeId)
	{
		PushbackStrikeId = State->StrikeId;
		StrikePushback = 0.f;
	}
	const float Allowed = MaxPushbackPerStrike - StrikePushback;
	if (Residual.SizeSquared() < 0.01f || Allowed <= 0.f) return;
	const FVector Step = (Residual * PushbackRate).GetClampedToMaxSize(FMath::Min(MaxPushbackPerFrame, Allowed));
	const FVector Before = GetOwner()->GetActorLocation();
	GetOwner()->AddActorWorldOffset(Step, true);
	const float Moved = float(FVector::Dist(Before, GetOwner()->GetActorLocation()));
	PushbackDistance += Moved;
	StrikePushback += Moved;
}

void URPGStrikeContactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USkeletalMeshComponent* Mesh = GetMesh();
	URPGAnimInstance* Anim = Mesh ? Cast<URPGAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Mesh || !Anim) return;

	// What the proxy did to this pose, and so where the animation alone has each limb.
	const FTransform& ToWorld = Mesh->GetComponentTransform();
	for (int32 I = 0; I < NumLimbs; ++I)
	{
		FLimbState& S = Limbs[I];
		const FLimbDef& Def = Limb(I);
		const FRPGLimbContactResult Result = Anim->GetContactResult(I);
		S.Correction = ToWorld.TransformVector(Result.Moved);
		S.Residual = ToWorld.TransformVector(Result.Residual);
		S.AnimatedAngle = Result.AnimatedAngle;
		S.CorrectedAngle = Result.CorrectedAngle;
		S.LastAnimated = S.Animated;
		S.LastAnimatedBone = S.AnimatedBone;
		S.Animated = SamplePoint(Mesh, Def.Samples[0]) - S.Correction;
		S.AnimatedBone = Mesh->GetBoneLocation(Def.End) - S.Correction;
	}

	const bool bActive = IsActive() && IsAttacking(DeltaTime) && DeltaTime > 0.f;
	TArray<UPrimitiveComponent*> Targets;
	if (bActive) GatherTargets(Targets);
	for (int32 I = 0; I < NumLimbs; ++I)
	{
		FLimbState& S = Limbs[I];
		if (bActive && S.bHaveLast) UpdateLimb(I, Targets, DeltaTime);
		else { S.Latches.Reset(); S.Set.Num = 0; S.ApproachSpeed = 0.f; }
		S.bHaveLast = DeltaTime > 0.f;
	}
	PushPlanes();
	if (bActive) PushBack(DeltaTime);

	// rpg.StrikeContactDebug 2: log every held contact (bone, which sphere made it, how far the fist / foot centre is in
	// front of it) each frame.
	if (CVarStrikeContactDebug.GetValueOnGameThread() >= 2)
		for (int32 I = 0; I < NumLimbs; ++I)
		{
			const FLimbState& S = Limbs[I];
			if (S.Latches.Num() == 0) continue;
			FString Row;
			const FVector Centre = SamplePoint(Mesh, Limb(I).Samples[0]);
			for (int32 L = 0; L < S.Latches.Num() && L < S.Set.Num; ++L)
				Row += FString::Printf(TEXT(" %s/s%d gap %.1f n(%.2f,%.2f,%.2f)"), *S.Latches[L].Bone.ToString(), S.Latches[L].Sample,
					float(FVector::DotProduct(Centre - S.Set.Planes[L].Point, S.Set.Planes[L].Normal)) - Limb(I).Samples[0].Radius,
					S.Set.Planes[L].Normal.X, S.Set.Planes[L].Normal.Y, S.Set.Planes[L].Normal.Z);
			UE_LOG(LogTemp, Display, TEXT("RPGCONTACTLATCH limb %d corr %.1f:%s"), I, S.Correction.Size(), *Row);
		}

	if ((bDrawDebug || CVarStrikeContactDebug.GetValueOnGameThread() > 0) && GetWorld())
		for (const FLimbState& S : Limbs)
		{
			if (S.Set.Num == 0 && S.Correction.IsNearlyZero(0.1f)) continue;
			DrawDebugSphere(GetWorld(), S.Animated, 3.f, 8, FColor::Red, false, 0.f);
			DrawDebugSphere(GetWorld(), S.Animated + S.Correction, 3.f, 8, FColor::Green, false, 0.f);
			for (int32 P = 0; P < S.Set.Num; ++P)
			{
				const FRPGLimbContactPlane& Plane = S.Set.Planes[P];
				DrawDebugSolidPlane(GetWorld(), FPlane(Plane.Point, Plane.Normal), Plane.Point, FVector2D(8.f, 8.f), FColor(255, 200, 0, 60), false, 0.f);
				DrawDebugDirectionalArrow(GetWorld(), Plane.Point, Plane.Point + Plane.Normal * 10.f, 4.f, FColor::Orange, false, 0.f);
			}
		}
}
