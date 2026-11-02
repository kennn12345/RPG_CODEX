#include "RPGStrikeContactComponent.h"

#include "RPGAnimInstance.h"
#include "RPGCombatStateComponent.h"
#include "RPGCharacterContactComponent.h"
#include "RPGPrototypeAnimationComponent.h"
#include "RPGMeleeTarget.h"
#include "RPGMeleeHitComponent.h"
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
	bool AllowRecoveryContact(const AActor* Owner, const AActor* Target, int32 LimbIndex)
	{
		if (!URPGCharacterContactComponent::IsRecovering(Target)) return true;
		const auto* State = Owner->FindComponentByClass<URPGCombatStateComponent>();
		// A rising body may still be struck. Planted feet and the other resting limbs must not follow its bones.
		return State && IndexOf(State->StrikeLimb) == LimbIndex;
	}

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
    UPrimitiveComponent* Target=nullptr;FName Bone;FVector Point,Normal,Travel;
    return GetStrikeContact(Limb,Target,Bone,Point,Normal,Travel);
}

FVector URPGStrikeContactComponent::GetAnimatedTravel(ERPGStrikeLimb Limb, int32 SurfaceSample) const
{
	const int32 Index = IndexOf(Limb);
	if (Index == INDEX_NONE || !Limbs[Index].bHaveLast) return FVector::ZeroVector;
	if(Index<=LeftHand)return SurfaceSample==1?Limbs[Index].AnimatedKnuckle-Limbs[Index].LastAnimatedKnuckle
        :Limbs[Index].Animated-Limbs[Index].LastAnimated;
    return Limbs[Index].AnimatedBone-Limbs[Index].LastAnimatedBone;
}

bool URPGStrikeContactComponent::GetContact(ERPGStrikeLimb Limb, UPrimitiveComponent*& OutTarget, FName& OutBone, FVector& OutPoint, FVector& OutNormal) const
{
    FVector Travel;
    return FindContact(Limb,OutTarget,OutBone,OutPoint,OutNormal,Travel,false);
}

bool URPGStrikeContactComponent::GetStrikeContact(ERPGStrikeLimb Limb,UPrimitiveComponent*& OutTarget,FName& OutBone,FVector& OutPoint,FVector& OutNormal,FVector& OutTravel) const
{
    return FindContact(Limb,OutTarget,OutBone,OutPoint,OutNormal,OutTravel,true);
}

bool URPGStrikeContactComponent::FindContact(ERPGStrikeLimb Limb,UPrimitiveComponent*& OutTarget,FName& OutBone,FVector& OutPoint,FVector& OutNormal,FVector& OutTravel,bool bRequireStrike) const
{
	const int32 Index = IndexOf(Limb);
	if (Index == INDEX_NONE || Limbs[Index].Latches.Num() == 0 || Limbs[Index].Set.Num == 0) return false;
    if(bRequireStrike && Index>LeftHand && Limbs[Index].ApproachSpeed<MinStrikeSpeed)return false;
    // Latches are predictive constraints for the next pose, not proof that this pose has landed. In particular,
    // a forearm plane on the chest can precede the fist's head contact. Only a physical end-segment contact counts
    // as a punch; close kicks may still land with the knee/shin. Body-array order must not choose the damage zone.
    const USkeletalMeshComponent* Mesh=GetMesh();
    if(!Mesh)return false;
    const FLimbDef& Def=RPGStrikeContact::Limb(Index);
    float BestGap=TNumericLimits<float>::Max();
    bool bFound=false;
    for(const FLatch& Latch:Limbs[Index].Latches)
    {
        auto* Target=Cast<USkeletalMeshComponent>(Latch.Target.Get());
        const FBodyInstance* Body=Target?Target->GetBodyInstance(Latch.Bone):nullptr;
        if(!Body)continue;
        const int32 First=Index<=LeftHand?0:Latch.Sample;
        const int32 End=Index<=LeftHand?2:Latch.Sample+1;
        // The predicted sample is only a constraint hint. Any real knuckle/fist-centre touch may land on this body.
        for(int32 K=First;K<End;++K)
        {
            const FSurfaceSample& Sample=K<3?Def.Samples[K]:Def.Inner[K-3];
            const FVector Now=SamplePoint(Mesh,Sample);
            FVector Closest;float DistSq=0.f;
            if(!Body->GetSquaredDistanceToBody(Now,DistSq,Closest))continue;
            const float Gap=FMath::Sqrt(DistSq)-Sample.Radius;
            if(Gap>.5f || Gap>=BestGap)continue;
            FVector Normal=(Now-Closest).GetSafeNormal();
            if(DistSq<=.01f)
            {
                FMTDResult MTD;
                if(!Body->OverlapTest(Now,FQuat::Identity,FCollisionShape::MakeSphere(Sample.Radius),&MTD))continue;
                Normal=MTD.Direction.GetSafeNormal();Closest=Now+Normal*(MTD.Distance-Sample.Radius);
            }
            const FVector Travel=GetAnimatedTravel(Limb,K);
            const float Dt=GetWorld()?GetWorld()->GetDeltaSeconds():0.f;
            // A rotating fist's knuckles and palm have different velocities. A palm's tangent motion must not
            // reject a real incoming knuckle touch, or let a different body consume that strike first.
            if(bRequireStrike && Index<=LeftHand && (Dt<=0.f || FVector::DotProduct(Travel/Dt,-Normal)<MinStrikeSpeed))continue;
            BestGap=Gap;bFound=true;OutTarget=Target;OutBone=Latch.Bone;OutPoint=Closest;OutNormal=Normal;OutTravel=Travel;
        }
    }
    return bFound;
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
	const auto* Animation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();
	const bool bReferenceFist=Index<=LeftHand && Animation && Animation->IsReferencePunchPlaying();
	const FVector Acceleration=(Velocity-S.PreviousStep).GetClampedToMaxSize(15.f);
	// The reference release accelerates over four filmed frames. Constant-speed
	// lookahead can miss its first surface; predict the increasing step as well.
	// Keep the filmed fist's prediction horizon at 33 ms. Two frames is 67 ms at 30 Hz: projecting a curved
    // overhand that far as a straight line latches unrelated torso/arm planes and can stop a genuine head shot.
    const auto* CombatState=GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
    const bool bFilmedFist=Index<=LeftHand && Animation && Animation->IsCapturedStandardPunchPlaying();
    const auto* Melee=bFilmedFist?GetOwner()->FindComponentByClass<URPGMeleeHitComponent>():nullptr;
    // Predictive arm constraints must not start an attack-strength slide before the fist actually lands.
    // The landing applies its normal Power impulse; sustained surface pressure may follow that confirmed strike.
    const bool bCanPush=!bFilmedFist || (Melee && Melee->ConfirmedHitCount>0 && CombatState
        && Melee->LastHit.StrikeId==CombatState->StrikeId && Melee->LastHitTime>=CombatState->LastReleaseTime);
    const float Steps=bFilmedFist?(1.f/30.f)/DeltaTime:2.f;
    const FVector LookAhead=Velocity*Steps+(bReferenceFist && FVector::DotProduct(Acceleration,Velocity)>0.f?Acceleration*(.5f*Steps*Steps):FVector::ZeroVector);

	const bool bFastRelease=bReferenceFist && !Animation->IsReferenceChargedPunchPlaying() && CombatState && CombatState->AttackPhase==ERPGAttackPhase::Strike;
	const float CurveMargin=bFastRelease && Velocity.SizeSquared()>1.f?FMath::Clamp(float(Velocity.Size()*.25f+Acceleration.Size()*.5f),2.f,8.f):0.f;
	for (int32 K = 0; K < NumSamples; ++K)
	{
		Now[K] = SamplePoint(Mesh, Samples[K]);
		Next[K] = Now[K] - (K < 3 ? S.Correction : FVector::ZeroVector) + LookAhead;
	}

	// Contacts already held: each moves with its body; it lets go once the animated limb is back out in front of it.
	for (int32 L = S.Latches.Num() - 1; L >= 0; --L)
	{
		const FLatch& Latch = S.Latches[L];
		FTransform BT;
		bool bKeep = Latch.Target.IsValid() && AllowRecoveryContact(GetOwner(), Latch.Target->GetOwner(), Index) && BodyTransform(Latch.Target.Get(), Latch.Bone, BT);
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
		if (!Target || !AllowRecoveryContact(GetOwner(), Target->GetOwner(), Index)) continue;
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
				// A conservative query envelope covers the curved release between
				// frames. Hit points remain on the target's real surface; physical
				// limb radii and allowed compression are unchanged.
				const FCollisionShape PredictSphere = FCollisionShape::MakeSphere(Samples[K].Radius+CurveMargin);
				FHitResult Hit;
				if (FVector::DistSquared(Now[K], Next[K]) > 0.01f && Body.Key->Sweep(Hit, Now[K], Next[K], FQuat::Identity, PredictSphere, false))
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
        Plane.Sample=Latch.Sample;
        Plane.NumCapsules=0;
        if(const auto* TargetMesh=Cast<USkeletalMeshComponent>(Latch.Target.Get()))
            if(const auto* Body=TargetMesh->GetBodyInstance(Latch.Bone);Body && Body->BodySetup.IsValid() && Body->BodySetup->IsA<UBodySetup>())
            {
                const auto& G=CastChecked<UBodySetup>(Body->BodySetup.Get())->AggGeom;
                // Copy only complete sphere/capsule bodies. Complex/box bodies keep their existing contact planes.
                if(G.BoxElems.IsEmpty() && G.ConvexElems.IsEmpty() && G.SphereElems.Num()+G.SphylElems.Num()<=4)
                {
                    const FTransform BodyWorld=Body->GetUnrealWorldTransform();
                    for(const auto& Elem:G.SphereElems)
                    {
                        const auto ScaledShape=Elem.GetFinalScaled(Body->Scale3D,FTransform::Identity);
                        auto& C=Plane.Capsules[Plane.NumCapsules++];C.A=C.B=BodyWorld.TransformPosition(ScaledShape.Center);C.Radius=ScaledShape.Radius;
                    }
                    for(const auto& Elem:G.SphylElems)
                    {
                        const auto ScaledShape=Elem.GetFinalScaled(Body->Scale3D,FTransform::Identity);
                        const FTransform T=ScaledShape.GetTransform()*BodyWorld;
                        const FVector Axis=T.TransformVectorNoScale(FVector(0,0,ScaledShape.Length*.5f));
                        auto& C=Plane.Capsules[Plane.NumCapsules++];C.A=T.GetLocation()-Axis;C.B=T.GetLocation()+Axis;C.Radius=ScaledShape.Radius;
                    }
                }
            }
		// A struck target's small hand/forearm bodies can rebound farther than
		// its torso. Reference fists anticipate that motion with the same
		// measured velocity rather than waiting until the next pose is inside.
		// Anticipate an approaching surface. Predicting a yielding surface away
		// pulls pressure off too soon and then reapplies it on a small rebound.
		const FVector Lead=bReferenceFist?N*FMath::Max(0.f,float(FVector::DotProduct(SurfaceVelocity,N)))*DeltaTime:SurfaceVelocity*DeltaTime;
		Plane.Point = P + Lead.GetClampedToMaxSize(bReferenceFist?12.f:MaxPlaneLead);
		Plane.Normal = N;
		const float Into = float(FVector::DotProduct(SurfaceVelocity - Velocity / DeltaTime, N));
		S.ApproachSpeed = FMath::Max(S.ApproachSpeed, Into);
		// Still driving in: the target is pushed (it can give way so the strike follows through).
		AActor* TargetActor = Latch.Target.IsValid() ? Latch.Target->GetOwner() : nullptr;
        const FSurfaceSample& TouchSample=Samples[Latch.Sample];
        const auto* TargetMesh=Cast<USkeletalMeshComponent>(Latch.Target.Get());
        const FBodyInstance* TouchBody=TargetMesh?TargetMesh->GetBodyInstance(Latch.Bone):nullptr;
        FVector Closest;float DistSq=0.f;
        const bool bTouching=TouchBody && TouchBody->GetSquaredDistanceToBody(Now[Latch.Sample],DistSq,Closest)
            && DistSq<=FMath::Square(TouchSample.Radius+.5f);
        if(bCanPush && bTouching && Into>0.f && TargetActor && TargetActor->Implements<URPGMeleeTarget>())
            IRPGMeleeTarget::Execute_ReceiveContactPush(TargetActor,-N*Into,Latch.Bone);
	}
}

void URPGStrikeContactComponent::PushPlanes()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return;
	TArray<USkeletalMeshComponent*> Meshes;
	Character->GetComponents<USkeletalMeshComponent>(Meshes);
	const auto* Animation=Character->FindComponentByClass<URPGPrototypeAnimationComponent>();
	const bool SmoothFist=Animation && (Animation->IsCapturedStandardPunchPlaying() || Animation->IsReferencePunchPlaying());
	// A charged reference drive into a close target builds a correction up to ~45 cm; unwinding that over .2 s let go
	// at up to 6.5 cm a frame. Give charged releases .3 s.
	const bool Charged=Animation && Animation->IsReferenceChargedPunchPlaying();
	float UnloadTime=SmoothFist?FMath::Max(ReleaseTime,Charged?.3f:.2f):ReleaseTime;
	if(ReferenceReleaseRemaining>0.f && !(Animation && Animation->IsReferencePunchPlaying()))
		for(const auto& LimbState:Limbs)
			if(LimbState.Correction.SizeSquared()>400.f)UnloadTime=FMath::Max(UnloadTime,.12f);
	// The visible body and the first-person shadow body (same surfaces, so the shadow's limbs stop in the same place).
	for (USkeletalMeshComponent* Mesh : Meshes)
		if (URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Mesh->GetAnimInstance()))
		{
            for(int32 I=0;I<NumLimbs;++I)
            {
                Anim->ContactSets[I]=Limbs[I].Set;
                if(const auto* MainMesh=GetMesh())for(int32 K=0;K<3;++K)Anim->ContactSets[I].StartSamples[K]=SamplePoint(MainMesh,Limb(I).Samples[K]);
            }
			// Keep immediate push-out; unload reference full-body punches gradually as their body drive withdraws.
			// This avoids small pressure reversals while preserving the same surface and compression limits.
			Anim->ContactReleaseTime = UnloadTime;
			Anim->bReferenceArmContact=(Animation && Animation->IsReferencePunchPlaying()) || ReferenceReleaseRemaining>0.f;
			// Keep impact pressure through a brief head rebound instead of
			// alternating withdrawal/re-contact over consecutive60Hz poses,
			// including brief gaps between neighboring surface contacts.
			Anim->ContactPressureHold = Animation && Animation->IsReferencePunchPlaying()?(Animation->IsReferenceChargedPunchPlaying()?.16f:.10f):0.f;
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
	const auto* Animation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();
	// A release and the following hit window are one reference punch, not two
	// independent opportunities to consume the retreat budget.
	const int32 BudgetKey=Animation && Animation->IsReferencePunchPlaying()
		?-Animation->CapturedPunchReleaseCount-1:State?State->StrikeId:PushbackStrikeId;
	if (BudgetKey != PushbackStrikeId)
	{
		PushbackStrikeId = BudgetKey;
		StrikePushback = 0.f;
	}
	const bool FullDrive=Animation && Animation->IsReferenceChargedPunchPlaying() && State
		&& (State->AttackPhase==ERPGAttackPhase::Strike || State->AttackPhase==ERPGAttackPhase::Recovery);
	const bool Power=FullDrive && Animation->LastCapturedPunchTier==2;
	const bool Standard=Animation && Animation->IsReferencePunchPlaying() && !Animation->IsReferenceChargedPunchPlaying() && State
		&& (State->AttackPhase==ERPGAttackPhase::Strike || State->AttackPhase==ERPGAttackPhase::Recovery);
	// A close target cannot absorb the larger reference charge's hip/shoulder drive
	// by folding the arm alone. Let the player's existing swept back-off take it,
	// without moving the target or changing limb radii/compression/fold limits.
	const float StrikeLimit=FullDrive?FMath::Max(MaxPushbackPerStrike,Power?26.f:18.f):Standard?FMath::Max(MaxPushbackPerStrike,22.f):MaxPushbackPerStrike;
	const float FrameLimit=FullDrive?FMath::Max(MaxPushbackPerFrame,Power?4.5f:3.f):Standard?FMath::Max(MaxPushbackPerFrame,3.5f):MaxPushbackPerFrame;
	const float Rate=FullDrive?FMath::Max(PushbackRate,Power?1.f:.8f):Standard?FMath::Max(PushbackRate,1.f):PushbackRate;
	const float Allowed = StrikeLimit - StrikePushback;
	if (Residual.SizeSquared() < 0.01f || Allowed <= 0.f) return;
	const FVector Step = (Residual * Rate).GetClampedToMaxSize(FMath::Min(FrameLimit, Allowed));
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
	const auto* Animation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>();
	const auto* CombatState=GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
	if(Animation && Animation->IsReferencePunchPlaying())ReferenceReleaseRemaining=.25f;
	else if(CombatState && CombatState->AttackPhase!=ERPGAttackPhase::None)ReferenceReleaseRemaining=0.f;
	else ReferenceReleaseRemaining=FMath::Max(0.f,ReferenceReleaseRemaining-DeltaTime);

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
		S.PreviousStep = S.Animated-S.LastAnimated;
		S.LastAnimated = S.Animated;
		S.LastAnimatedBone = S.AnimatedBone;
        S.LastAnimatedKnuckle=S.AnimatedKnuckle;
		S.Animated = I<=LeftHand && Result.bHaveAnimatedFist?ToWorld.TransformPosition(Result.AnimatedFist)
            :SamplePoint(Mesh,Def.Samples[0])-S.Correction;
		S.AnimatedBone = Mesh->GetBoneLocation(Def.End) - S.Correction;
        S.AnimatedKnuckle=I<=LeftHand && Result.bHaveAnimatedFist?ToWorld.TransformPosition(Result.AnimatedKnuckle)
            :SamplePoint(Mesh,Def.Samples[1])-S.Correction;
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
    if (bActive) PushBack(DeltaTime);
    PushPlanes();
    if(bActive)
        if(auto* PlayingAnimation=GetOwner()->FindComponentByClass<URPGPrototypeAnimationComponent>())PlayingAnimation->RefreshFilmedContactWindow();

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
