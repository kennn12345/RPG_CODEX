#include "RPGAnimInstance.h"

#include "Animation/AnimMontageEvaluationState.h"
#include "Animation/AnimNodeBase.h"
#include "RPGPrototypeAnimationComponent.h"
#include "TwoBoneIK.h"
#include "HAL/IConsoleManager.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPoleTrace(TEXT("rpg.ContactPoleTrace"), 0,
		TEXT("Log right-punch pole rotation and local transform changes."));
	/** Share of the aim each spine bone takes, pelvis up (sums to 1): the upper chest turns most, the waist least. */
	struct FSpineShare { const TCHAR* Bone; float Yaw; float Pitch; };
	constexpr FSpineShare SpineShares[] = {
		{TEXT("spine_01"), 0.10f, 0.15f},
		{TEXT("spine_02"), 0.15f, 0.20f},
		{TEXT("spine_03"), 0.20f, 0.20f},
		{TEXT("spine_04"), 0.25f, 0.20f},
		{TEXT("spine_05"), 0.30f, 0.25f},
	};

	/** Most the strike contact moves a hand / foot off its animation, cm (a strike lands at most this deep). */
	constexpr float MaxContactCorrection = 45.f;

	/** Component-space transform of a bone: its parents' transforms times its local one. */
	FTransform BoneComponentTransform(const FCompactPose& Pose, FCompactPoseBoneIndex Index)
	{
		FTransform Result = Pose[Index];
		const FBoneContainer& Bones = Pose.GetBoneContainer();
		for (FCompactPoseBoneIndex Up = Bones.GetParentBoneIndex(Index); Up.IsValid(); Up = Bones.GetParentBoneIndex(Up))
			Result = Result * Pose[Up];
		return Result;
	}
}

FRPGLimbContactResult URPGAnimInstance::GetContactResult(int32 Limb) const
{
	// (waits for a pose still being evaluated on a worker thread)
	return GetProxyOnGameThread<FRPGAnimInstanceProxy>().GetContactResult(Limb);
}

void URPGAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (URPGPrototypeAnimationComponent* Source = MirrorFrom.Get()) Source->MirrorMontagesTo(this);
}

void FRPGAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const URPGAnimInstance* Instance = Cast<URPGAnimInstance>(InAnimInstance))
	{
		AimYaw = Instance->AimYaw;
		AimPitch = Instance->AimPitch;
		bPitchAboutPivot = Instance->bPitchAboutPivot;
		PitchPivot = Instance->PitchPivot;
		GuardSteady = Instance->GuardSteady;
		GuardChestRest = Instance->GuardChestRest;
		GuardSteadyMontage = Instance->GuardSteadyMontage;
		for (int32 I = 0; I < RPGStrikeContact::NumLimbs; ++I) ContactSets[I] = Instance->ContactSets[I];
		ContactReleaseTime = Instance->ContactReleaseTime;
		FlowBones = Instance->FlowBones;
		FlowRotations = Instance->FlowRotations;
	}
}

bool FRPGAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	// Only the main graph; linked layers evaluate as usual.
	if (InRootNode != GetRootNode()) return false;
	EvaluateAnimationNode_WithRoot(Output, InRootNode);
	ApplyGuardSteady(Output);   // before the aim, which then swings the steadied chest about the eyes
	ApplySpineAim(Output);
	ApplyFlow(Output);
	ApplyStrikeContact(Output);   // last: on the final arm and leg positions
	return true;
}

void FRPGAnimInstanceProxy::ApplyGuardSteady(FPoseContext& Output) const
{
	if (GuardSteady <= 0.f || !GuardSteadyMontage) return;
	float Weight = 0.f;
	for (const FMontageEvaluationState& State : GetMontageEvaluationData())
		if (State.Montage.Get() == GuardSteadyMontage) Weight = State.BlendInfo.GetBlendedValue();
	if (Weight <= 0.f) return;
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};
	const FCompactPoseBoneIndex Chest = Find(TEXT("spine_05"));
	if (!Chest.IsValid()) return;
	// The shoulders (not the chest) take the correction, so the neck, head and camera stay where the body puts them.
	// Capped: a jump or fall isn't the guard's business.
	const FTransform ChestCS = BoneComponentTransform(Pose, Chest);
	const FVector Shift = ((GuardChestRest - ChestCS.GetTranslation()) * (GuardSteady * Weight)).GetClampedToMaxSize(12.f);
	const FVector Local = ChestCS.GetRotation().UnrotateVector(Shift);   // in the chest's space: the clavicles' parent
	for (const TCHAR* Clavicle : {TEXT("clavicle_l"), TEXT("clavicle_r")})
	{
		const FCompactPoseBoneIndex Index = Find(Clavicle);
		if (Index.IsValid() && Bones.GetParentBoneIndex(Index) == Chest) Pose[Index].AddToTranslation(Local);
	}
}

void FRPGAnimInstanceProxy::ApplyFlow(FPoseContext& Output) const
{
	// Parents first (the list is in that order): each bone turned in component space about its own pivot.
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	for (int32 I = 0; I < FlowBones.Num() && I < FlowRotations.Num(); ++I)
	{
		const float Angle = float(FlowRotations[I].Size());
		if (Angle < 1.e-4f) continue;
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FlowBones[I]);
		if (MeshIndex == INDEX_NONE) continue;
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (!Index.IsValid()) continue;
		FQuat Parent = FQuat::Identity;
		for (FCompactPoseBoneIndex Up = Bones.GetParentBoneIndex(Index); Up.IsValid(); Up = Bones.GetParentBoneIndex(Up))
			Parent = Pose[Up].GetRotation() * Parent;
		const FQuat Delta(FlowRotations[I] / Angle, Angle);
		FTransform& Local = Pose[Index];
		Local.SetRotation((Parent.Inverse() * Delta * Parent * Local.GetRotation()).GetNormalized());
	}
}

void FRPGAnimInstanceProxy::ApplySpineAim(FPoseContext& Output) const
{
	if (FMath::IsNearlyZero(AimYaw, 0.01f) && FMath::IsNearlyZero(AimPitch, 0.01f)) return;
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();

	// Mesh space (Manny): +Y forward, +X character-left, +Z up. A bone's component rotation is its parents' rotations
	// times its local one. Turning a bone by D in component space, about its own pivot, means its local rotation
	// becomes (P^-1 D P) L, P being its parent's component rotation (already turned by the bones below, so each bone adds
	// its share on top of theirs).
	float YawSoFar = 0.f;
	FCompactPoseBoneIndex Chest(INDEX_NONE);
	for (const FSpineShare& Share : SpineShares)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Share.Bone));
		if (MeshIndex == INDEX_NONE) continue;
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (!Index.IsValid()) continue;
		Chest = Index;

		FQuat Parent = FQuat::Identity;
		for (FCompactPoseBoneIndex Up = Bones.GetParentBoneIndex(Index); Up.IsValid(); Up = Bones.GetParentBoneIndex(Up))
			Parent = Pose[Up].GetRotation() * Parent;

		const float Yaw = bPitchAboutPivot ? 0.f : AimYaw * Share.Yaw;
		YawSoFar += Yaw;
		// Yaw + to the right turns +Y toward -X: a positive turn about +Z. Pitch + (up) turns +Y toward +Z: a positive
		// turn about the side axis, taken after this much of the yaw so the chest tips along the aimed direction.
		const FQuat YawTurn(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		const FVector Side = FQuat(FVector::UpVector, FMath::DegreesToRadians(YawSoFar)).RotateVector(FVector::XAxisVector);
		const float Pitch = bPitchAboutPivot ? 0.f : AimPitch * Share.Pitch;
		const FQuat Delta = FQuat(Side, FMath::DegreesToRadians(Pitch)) * YawTurn;
		FTransform& Local = Pose[Index];
		Local.SetRotation((Parent.Inverse() * Delta * Parent * Local.GetRotation()).GetNormalized());
	}

	// First person: the whole turn and tilt swing the upper chest about the eyes, so the arms keep their place in view.
	if (bPitchAboutPivot && Chest.IsValid())
	{
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(AimYaw));
		const FVector Side = Turn.RotateVector(FVector::XAxisVector);
		const FQuat Tilt = FQuat(Side, FMath::DegreesToRadians(AimPitch)) * Turn;
		const FTransform ParentCS = BoneComponentTransform(Pose, Bones.GetParentBoneIndex(Chest));
		FTransform ChestCS = Pose[Chest] * ParentCS;
		ChestCS.SetRotation((Tilt * ChestCS.GetRotation()).GetNormalized());
		ChestCS.SetTranslation(PitchPivot + Tilt.RotateVector(ChestCS.GetTranslation() - PitchPivot));
		Pose[Chest] = ChestCS.GetRelativeTransform(ParentCS);
	}
}

void FRPGAnimInstanceProxy::ApplyStrikeContact(FPoseContext& Output)
{
	using namespace RPGStrikeContact;
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};
	auto Angle = [](const FVector& A, const FVector& M, const FVector& E)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct((A - M).GetSafeNormal(), (E - M).GetSafeNormal())), -1.f, 1.f)));
	};
	const FTransform& ToWorld = GetComponentTransform();
	const float DeltaTime = GetDeltaSeconds();
	for (int32 L = 0; L < NumLimbs; ++L)
	{
		const FLimbDef& Def = Limb(L);
		const FRPGLimbContactSet& Set = ContactSets[L];
		FRPGLimbContactResult& Result = ContactResults[L];
		Result.Residual = FVector::ZeroVector;
		Result.Moved = FVector::ZeroVector;
		const FCompactPoseBoneIndex Root = Find(Def.Root), Mid = Find(Def.Mid), End = Find(Def.End);
		if (!Root.IsValid() || !Mid.IsValid() || !End.IsValid() || Bones.GetParentBoneIndex(End) != Mid || Bones.GetParentBoneIndex(Mid) != Root)
		{
			Result = FRPGLimbContactResult();
			continue;
		}
		const FCompactPoseBoneIndex RootParent = Bones.GetParentBoneIndex(Root);
		const FTransform ParentCS = RootParent.IsValid() ? BoneComponentTransform(Pose, RootParent) : FTransform::Identity;
		const FTransform RootCS = Pose[Root] * ParentCS;
		const FTransform MidCS = Pose[Mid] * RootCS;
		const FTransform EndCS = Pose[End] * MidCS;
		const FVector RootPos = RootCS.GetTranslation(), MidPos = MidCS.GetTranslation(), EndPos = EndCS.GetTranslation();
		Result.AnimatedAngle = Result.CorrectedAngle = Angle(RootPos, MidPos, EndPos);

		// A sample's position in this pose: the end bone, its children (fingers / ball of the foot), or the mid bone.
		auto At = [&](const TCHAR* Name, const FTransform& MidT, const FTransform& EndT) -> FVector
		{
			if (FCString::Strcmp(Name, Def.End) == 0) return EndT.GetTranslation();
			if (FCString::Strcmp(Name, Def.Mid) == 0) return MidT.GetTranslation();
			// fingers / ball of the foot: below the end bone (the knuckles via the metacarpal on the UE5 mannequin)
			FCompactPoseBoneIndex Bone = Find(Name);
			FTransform Local = FTransform::Identity;
			while (Bone.IsValid() && Bone != End)
			{
				Local = Local * Pose[Bone];
				Bone = Bones.GetParentBoneIndex(Bone);
			}
			return Bone == End ? (Local * EndT).GetTranslation() : EndT.GetTranslation();
		};
		auto Sample = [&](const FSurfaceSample& S, const FTransform& MidT, const FTransform& EndT)
		{
			return FMath::Lerp(At(S.A, MidT, EndT), At(S.B, MidT, EndT), S.T);
		};

		// The planes in component space.
		FVector PlaneP[MaxPlanes], PlaneN[MaxPlanes];
		for (int32 P = 0; P < Set.Num; ++P)
		{
			PlaneP[P] = ToWorld.InverseTransformPosition(Set.Planes[P].Point);
			PlaneN[P] = ToWorld.InverseTransformVectorNoScale(Set.Planes[P].Normal).GetSafeNormal();
		}

		// How far the hand / foot has to move so no sphere of the fist / foot is past any surface by more than the give
		// (softened, so contact starts without a kink). A few rounds, for corners where two surfaces meet.
		FVector Desired = FVector::ZeroVector;
		FVector EndSamples[3];
		for (int32 K = 0; K < 3; ++K) EndSamples[K] = Sample(Def.Samples[K], MidCS, EndCS);
		for (int32 Round = 0; Round < 3 && Set.Num > 0; ++Round)
			for (int32 P = 0; P < Set.Num; ++P)
			{
				float Need = 0.f;
				for (int32 K = 0; K < 3; ++K)
					Need = FMath::Max(Need, SoftPush(float(FVector::DotProduct(PlaneP[P] - EndSamples[K], PlaneN[P])) + Def.Samples[K].Radius, Def.Compression));
				const float Have = float(FVector::DotProduct(Desired, PlaneN[P]));
				if (Need > Have) Desired += PlaneN[P] * (Need - Have);
			}
		// Pushed out at once (never through the surface); eased off when the surface stops holding it.
		FVector& Applied = Result.Applied;
		if (Desired.SizeSquared() >= Applied.SizeSquared()) Applied = Desired;
		else Applied = Desired + (Applied - Desired) * FMath::Exp(-DeltaTime / FMath::Max(ContactReleaseTime, 0.005f));
		Applied = Applied.GetClampedToMaxSize(MaxContactCorrection);
		if (Applied.SizeSquared() < 1e-4f) Applied = FVector::ZeroVector;
		FTransform NewMidCS = MidCS, NewEndCS = EndCS;
		if (!Applied.IsZero())
		{

		// The elbow / knee bends no tighter than its fold limit (or the animation's own angle, if tighter): past that,
		// what is left over is the body's to take (Residual: the attacker backs off).
		FVector Target = EndPos + Applied;
		const float Upper = float(FVector::Dist(RootPos, MidPos)), Lower = float(FVector::Dist(MidPos, EndPos));
		const float FoldRad = FMath::DegreesToRadians(FMath::Min(Def.MinFold, Result.AnimatedAngle));
		const float MinReach = FMath::Sqrt(FMath::Max(0.f, Upper * Upper + Lower * Lower - 2.f * Upper * Lower * FMath::Cos(FoldRad)));
		const FVector FromRoot = Target - RootPos;
		if (FromRoot.Size() < MinReach && !FromRoot.IsNearlyZero())
		{
			const FVector Limited = RootPos + FromRoot.GetSafeNormal() * MinReach;
			Result.Residual += Target - Limited;
			Target = Limited;
		}

		// Two-bone IK to the moved hand / foot. The elbow / knee stays in the plane the animation has it in (its own
		// position is the pole); the wrist / ankle keeps the animation's angle to the forearm / shin.
		// An elbow / knee that would end up in a surface (a cross's raised elbow, a chambered knee, up close) is steered
		// out along that surface's normal: the pole moves out and the limb is solved again, so the arm / leg goes round
		// the target instead of through it.
		FVector NewMid, NewEnd;
		FVector Pole = MidPos;
		AnimationCore::SolveTwoBoneIK(RootPos, MidPos, EndPos, Pole, Target, NewMid, NewEnd, false, 1.0, 1.0);
		const FVector UnsteeredMid = NewMid;
		// Surface-driven elbow poles can reverse the arm around its reach axis.
		// Arms retain the authored elbow pole; the existing knee response is unchanged.
		for (int32 Round = 0; Round < 2 && L >= RightFoot; ++Round)
		{
			FVector Out = FVector::ZeroVector;
			for (int32 P = 0; P < Set.Num; ++P)
			{
				const float Depth = float(FVector::DotProduct(PlaneP[P] - NewMid, PlaneN[P])) + Def.Inner[0].Radius;
				if (Depth > 0.f) Out += PlaneN[P] * Depth;
			}
			if (Out.IsNearlyZero(0.1f)) break;
			Pole += Out * 2.f;

			AnimationCore::SolveTwoBoneIK(RootPos, MidPos, EndPos, Pole, Target, NewMid, NewEnd, false, 1.0, 1.0);
		}
		const FQuat TurnUpper = FQuat::FindBetweenNormals((MidPos - RootPos).GetSafeNormal(), (NewMid - RootPos).GetSafeNormal());
		const FVector LowerAfterUpper = TurnUpper.RotateVector(EndPos - MidPos).GetSafeNormal();
		const FQuat TurnLower = FQuat::FindBetweenNormals(LowerAfterUpper, (NewEnd - NewMid).GetSafeNormal());
		FTransform NewRootCS = RootCS;
		NewRootCS.SetRotation((TurnUpper * RootCS.GetRotation()).GetNormalized());
		NewMidCS.SetRotation((TurnLower * TurnUpper * MidCS.GetRotation()).GetNormalized());
		NewMidCS.SetTranslation(NewMid);
		NewEndCS.SetRotation((TurnLower * TurnUpper * EndCS.GetRotation()).GetNormalized());
		NewEndCS.SetTranslation(NewEnd);
		if (L == RightHand && CVarPoleTrace.GetValueOnAnyThread() != 0)
		{
			const FVector Reach = (NewEnd - RootPos).GetSafeNormal();
			const FVector BeforeBend = FVector::VectorPlaneProject(UnsteeredMid - RootPos, Reach).GetSafeNormal();
			const FVector AfterBend = FVector::VectorPlaneProject(NewMid - RootPos, Reach).GetSafeNormal();
			const float PoleAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(BeforeBend, AfterBend)), -1.f, 1.f)));
			const FTransform RootLocal = NewRootCS.GetRelativeTransform(ParentCS), MidLocal = NewMidCS.GetRelativeTransform(NewRootCS), EndLocal = NewEndCS.GetRelativeTransform(NewMidCS);
			UE_LOG(LogTemp, Display, TEXT("RPGPOLE mesh %s frame %llu steering %d applied %.2f angle %.2f elbowShift %.2f localTranslationErrors %.4f/%.4f/%.4f localScaleErrors %.4f/%.4f/%.4f mid(%.2f,%.2f,%.2f)"),
				*GetSkelMeshComponent()->GetName(), GFrameCounter, 0, Applied.Size(), PoleAngle, FVector::Dist(UnsteeredMid, NewMid),
				FVector::Dist(RootLocal.GetTranslation(), Pose[Root].GetTranslation()), FVector::Dist(MidLocal.GetTranslation(), Pose[Mid].GetTranslation()), FVector::Dist(EndLocal.GetTranslation(), Pose[End].GetTranslation()),
				FVector::Dist(RootLocal.GetScale3D(), Pose[Root].GetScale3D()), FVector::Dist(MidLocal.GetScale3D(), Pose[Mid].GetScale3D()), FVector::Dist(EndLocal.GetScale3D(), Pose[End].GetScale3D()), NewMid.X, NewMid.Y, NewMid.Z);
		}
		Pose[Root] = NewRootCS.GetRelativeTransform(ParentCS);
		Pose[Mid] = NewMidCS.GetRelativeTransform(NewRootCS);
		Pose[End] = NewEndCS.GetRelativeTransform(NewMidCS);
		Result.CorrectedAngle = Angle(RootPos, NewMid, NewEnd);
		Result.Moved = NewEnd - EndPos;
		}

		// The elbow / knee or the forearm / shin still in a surface (a chambered knee against a close target): bending
		// cannot fix that, so it is the body's to take too.
		for (int32 P = 0; P < Set.Num; ++P)
		{
			float Depth = 0.f;
			for (const FSurfaceSample& Inner : Def.Inner)
				Depth = FMath::Max(Depth, float(FVector::DotProduct(PlaneP[P] - Sample(Inner, NewMidCS, NewEndCS), PlaneN[P])) + Inner.Radius - Def.Compression);
			const float Have = float(FVector::DotProduct(Result.Residual, PlaneN[P]));
			if (Depth > Have) Result.Residual += PlaneN[P] * (Depth - Have);
		}
	}
}
