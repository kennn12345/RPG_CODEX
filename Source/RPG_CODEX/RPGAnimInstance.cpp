#include "RPGAnimInstance.h"
#include "RPGFlightAnimationTypes.h"

#include "Animation/AnimMontageEvaluationState.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "GameFramework/Character.h"
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
	if (URPGPrototypeAnimationComponent* Source = MirrorFrom.Get())
    {
        Source->MirrorMontagesTo(this);
        // The first-person shadow is a second instance; keep the flight blend in step.
        const ACharacter* Character = Cast<ACharacter>(GetOwningActor());
        const URPGAnimInstance* Main = Character && Character->GetMesh() ? Cast<URPGAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr;
        if (Main && Main != this)
        {
            FlightClips = Main->FlightClips; FlightWeight = Main->FlightWeight;
            FlightPhase = Main->FlightPhase; FlightBlend = Main->FlightBlend;
            FlightTurn = Main->FlightTurn; FlightActionIndex = Main->FlightActionIndex;
            FlightActionTime = Main->FlightActionTime; FlightActionWeight = Main->FlightActionWeight;
            FlowBones = Main->FlowBones; FlowRotations = Main->FlowRotations;
            RearSway = Main->RearSway;
            for (int32 I=0;I<2;++I) SpellPose[I] = Main->SpellPose[I];
            DirectionalHover = Main->DirectionalHover;
        }
    }
}

void FRPGAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const URPGAnimInstance* Instance = Cast<URPGAnimInstance>(InAnimInstance))
	{
        // A recent punch can leave combat aim active during its stance timeout.
        // Hand the torso over to spell aim instead of adding both rotations.
        const float MeleeAimShare = 1.f-FMath::Clamp(FMath::Max(Instance->SpellPose[0].Weight,Instance->SpellPose[1].Weight),0.f,1.f);
		AimYaw = Instance->AimYaw*MeleeAimShare;
		AimPitch = Instance->AimPitch*MeleeAimShare;
		bPitchAboutPivot = Instance->bPitchAboutPivot;
		PitchPivot = Instance->PitchPivot;
		GuardSteady = Instance->GuardSteady;
		GuardChestRest = Instance->GuardChestRest;
		GuardSteadyMontage = Instance->GuardSteadyMontage;
		for (int32 I = 0; I < RPGStrikeContact::NumLimbs; ++I) ContactSets[I] = Instance->ContactSets[I];
		ContactReleaseTime = Instance->ContactReleaseTime;
        FlightClips.Reset();
        for (UAnimSequence* Clip : Instance->FlightClips) FlightClips.Add(Clip);
        FlightWeight = Instance->FlightWeight;
        FlightPhase = Instance->FlightPhase;
        FlightTurn = Instance->FlightTurn; FlightActionIndex = Instance->FlightActionIndex;
        FlightActionTime = Instance->FlightActionTime; FlightActionWeight = Instance->FlightActionWeight;
        FlightBlend = Instance->FlightBlend;
		FlowBones = Instance->FlowBones;
		FlowRotations = Instance->FlowRotations;
        RearSway = Instance->RearSway;
        for (int32 I=0;I<2;++I) SpellPose[I] = Instance->SpellPose[I];
        DirectionalHover = Instance->DirectionalHover;
	}
}

bool FRPGAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	// Only the main graph; linked layers evaluate as usual.
	if (InRootNode != GetRootNode()) return false;
	EvaluateAnimationNode_WithRoot(Output, InRootNode);
	ApplyFlight(Output);
    ApplyDirectionalHover(Output);
	ApplyGuardSteady(Output);   // before the aim, which then swings the steadied chest about the eyes
	ApplySpineAim(Output);
	ApplyFlow(Output);
    ApplyRearSway(Output);
    ApplySpellPose(Output);
	ApplyStrikeContact(Output);   // last: on the final arm and leg positions
	return true;
}

void FRPGAnimInstanceProxy::ApplyDirectionalHover(FPoseContext& Output) const
{
    const auto& H=DirectionalHover;
    const float Weight=FMath::Clamp(H.Weight,0.f,1.f);
    if(Weight<=0.f || !FlightClips.IsValidIndex(RPGFlight::Hover) || !FlightClips[RPGFlight::Hover])return;
    // Four airborne poses share the approved upright hover base, never a grounded run cycle.
    FPoseContext Hover(Output);Hover.ResetToRefPose();FAnimationPoseData HoverData(Hover),OutputData(Output);
    FlightClips[RPGFlight::Hover]->GetAnimationPose(HoverData,FAnimExtractContext(FlightPhase*FlightClips[RPGFlight::Hover]->GetPlayLength(),false));
    FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData,HoverData,1.f-Weight);
    FCompactPose& Pose=Output.Pose;const FBoneContainer& Bones=Pose.GetBoneContainer();
    auto Find=[&Bones](FName Name)
    {
        const int32 I=Bones.GetPoseBoneIndexForBoneName(Name);
        return I==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
    };
    const FVector Up=H.UpCS.GetSafeNormal(),Right=H.RightCS.GetSafeNormal(),Forward=FVector::CrossProduct(Right,Up).GetSafeNormal();
    auto Turn=[&](FCompactPoseBoneIndex I,const FQuat& Rotation)
    {
        if(!I.IsValid())return;
        const FTransform Parent=BoneComponentTransform(Pose,Bones.GetParentBoneIndex(I));
        Pose[I].SetRotation((Parent.GetRotation().Inverse()*Rotation*Parent.GetRotation()*Pose[I].GetRotation()).GetNormalized());
    };
    auto Rotate=[&](FName Bone,FVector Axis,float Degrees)
    { Turn(Find(Bone),FQuat(Axis,FMath::DegreesToRadians(Degrees*Weight))); };
    const auto Pelvis=Find(TEXT("pelvis")),L=Find(TEXT("upperarm_l")),R=Find(TEXT("upperarm_r"));
    if(Pelvis.IsValid() && L.IsValid() && R.IsValid())
    {
        const FVector Current=FVector::VectorPlaneProject(BoneComponentTransform(Pose,R).GetLocation()-BoneComponentTransform(Pose,L).GetLocation(),Up).GetSafeNormal();
        const float Yaw=FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(Current,Right),Up),FVector::DotProduct(Current,Right));
        Turn(Pelvis,FQuat(Right,FMath::DegreesToRadians(H.Forward*9.f*Weight))
            *FQuat(Forward,FMath::DegreesToRadians(-H.Right*H.SideBodyLean*Weight))*FQuat(Up,Yaw*Weight));
        const FTransform Parent=BoneComponentTransform(Pose,Bones.GetParentBoneIndex(Pelvis));
        Pose[Pelvis].AddToTranslation(Parent.GetRotation().UnrotateVector(Up)*H.Bob*Weight);
    }
    Rotate(TEXT("spine_03"),Right,-H.Forward*2.f);
    Rotate(TEXT("spine_03"),Up,H.Turn*8.f);
    Rotate(TEXT("spine_01"),Forward,-H.Turn*3.f);
    Rotate(TEXT("spine_03"),Forward,H.ArmSide*H.SideChestCounterLean);
    for(int32 Side:{-1,1})
    {
        const FString S=Side<0?TEXT("l"):TEXT("r");
        // Pose the leg first: its whole bent chain opens toward travel at the hip.
        Rotate(*("thigh_"+S),Right,H.Forward*8.f);
        Rotate(*("thigh_"+S),Forward,-H.Right*5.f);
        Rotate(*("calf_"+S),Right,-FMath::Abs(H.Forward)*4.f-FMath::Abs(H.Right)*3.f);
        Rotate(*("foot_"+S),Right,H.Forward*3.f);
        const float Lead=FMath::SmoothStep(0.1f,0.8f,H.Right*Side);
        Rotate(*("thigh_"+S),Right,-Lead*H.SideKneeLift);
        Rotate(*("calf_"+S),Right,Lead*H.SideKneeBend);
        Rotate(*("foot_"+S),Right,-Lead*8.f);
        const float Out=Lead*float(H.KneeOut[Side<0?0:1]);
        Rotate(*("thigh_"+S),Forward,Side*Out*H.SideKneeOut);
        Rotate(*("thigh_"+S),Up,Side*Out*H.SideHipOpen-H.Turn*5.f);
        Rotate(*("calf_"+S),Right,FMath::Abs(H.Turn)*2.f);
        const auto KneeBone=Find(*("calf_"+S)),HipBone=Find(*("thigh_"+S)),FootBone=Find(*("foot_"+S));
        const float Tuck=FMath::DegreesToRadians(H.SideFootTuck*float(H.FootTuck[Side<0?0:1]))*Weight;
        if(Tuck>0.f && KneeBone.IsValid() && HipBone.IsValid() && FootBone.IsValid())
        {
            const FVector UpperLeg=(BoneComponentTransform(Pose,KneeBone).GetLocation()-BoneComponentTransform(Pose,HipBone).GetLocation()).GetSafeNormal();
            const FVector LowerLeg=(BoneComponentTransform(Pose,FootBone).GetLocation()-BoneComponentTransform(Pose,KneeBone).GetLocation()).GetSafeNormal();
            const FVector Hinge=FVector::CrossProduct(UpperLeg,LowerLeg).GetSafeNormal();
            if(!Hinge.IsNearlyZero())
            {
                // Preserve the open knee's position and direction; bend only its anatomical hinge.
                Turn(KneeBone,FQuat(Hinge,Tuck));
                Turn(FootBone,FQuat(Hinge,-Tuck*.45f));
            }
        }

        const float FreeHand=1.f-FMath::Clamp(SpellPose[Side<0?0:1].Weight,0.f,1.f);
        const float Trail=FMath::SmoothStep(.1f,.8f,-H.ArmSide*Side);
        Rotate(*("upperarm_"+S),Forward,Side*(4.f+FMath::Abs(H.Forward)*3.f+FMath::Abs(H.Turn)*2.f)+H.Right*4.f+H.ArmSide*H.SideArmLean*FreeHand*(1.f-Trail));
        Rotate(*("lowerarm_"+S),Right,FMath::Max(-H.Forward,0.f)*6.f+H.Right*Side*2.f+FMath::Abs(H.ArmSide)*4.f*FreeHand);
        Rotate(*("lowerarm_"+S),Forward,H.ArmSide*3.f*FreeHand*(1.f-Trail));
        const auto Upper=Find(*("upperarm_"+S)),Wrist=Find(*("hand_"+S));
        const auto Hip=Find(*("thigh_"+S)),Ankle=Find(*("foot_"+S));
        if(Trail>0.f && FreeHand>0.f && Upper.IsValid() && Wrist.IsValid() && Hip.IsValid() && Ankle.IsValid())
        {
            const FVector Leg=BoneComponentTransform(Pose,Ankle).GetLocation()-BoneComponentTransform(Pose,Hip).GetLocation();
            const FVector ArmLine=BoneComponentTransform(Pose,Wrist).GetLocation()-BoneComponentTransform(Pose,Upper).GetLocation();
            const float LegAngle=FMath::Atan2(FVector::DotProduct(Leg,Right),-FVector::DotProduct(Leg,Up));
            const float ArmAngle=FMath::Atan2(FVector::DotProduct(ArmLine,Right),-FVector::DotProduct(ArmLine,Up));
            const float PlaneLength=FMath::Max(1.f,float(FVector::VectorPlaneProject(ArmLine,Forward).Size()));
            const float SpacedLegAngle=FMath::Asin(FMath::Clamp(FMath::Sin(LegAngle)+Side*H.TrailingArmSpacing/PlaneLength,-.98f,.98f));
            const float Goal=FMath::Lerp(ArmAngle,SpacedLegAngle,Trail);
            // Keep the trailing elbow/hand outside the shoulder line as it follows its same-side leg.
            const float ClearGoal=Side*FMath::Max(Side*Goal,FMath::DegreesToRadians(H.TrailingArmMinOut)*Trail);
            const float Delta=FMath::Clamp(FMath::UnwindRadians(ClearGoal-ArmAngle),-PI/5.f,PI/5.f);
            Turn(Upper,FQuat(Forward,Delta*Weight*FreeHand));
        }
        // Relaxed hover: pronate the forearm toward the floor, then add only a small wrist bend.
        // Leave the casting hand to its existing outward-facing spell pose.
        const float RelaxedWeight=Weight*(1.f-FMath::Clamp(SpellPose[Side<0?0:1].Weight,0.f,1.f));
        const auto Arm=Find(*("lowerarm_"+S)),Hand=Find(*("hand_"+S));
        const auto Middle=Find(*("middle_01_"+S)),Index=Find(*("index_01_"+S)),Pinky=Find(*("pinky_01_"+S));
        if(RelaxedWeight>0.f && Arm.IsValid() && Hand.IsValid() && Middle.IsValid() && Index.IsValid() && Pinky.IsValid())
        {
            auto PalmNormal=[&]()
            {
                const FVector Finger=(BoneComponentTransform(Pose,Middle).GetLocation()-BoneComponentTransform(Pose,Hand).GetLocation()).GetSafeNormal();
                const FVector Across=(BoneComponentTransform(Pose,Index).GetLocation()-BoneComponentTransform(Pose,Pinky).GetLocation()).GetSafeNormal();
                return FVector::CrossProduct(Finger,Across).GetSafeNormal()*float(Side);
            };
            const FVector ForearmAxis=(BoneComponentTransform(Pose,Hand).GetLocation()-BoneComponentTransform(Pose,Arm).GetLocation()).GetSafeNormal();
            const FVector DownProjected=FVector::VectorPlaneProject(-Up,ForearmAxis);
            const FVector PalmProjected=FVector::VectorPlaneProject(PalmNormal(),ForearmAxis).GetSafeNormal();
            if(DownProjected.SizeSquared()>0.01f && !PalmProjected.IsNearlyZero())
            {
                const FVector Goal=DownProjected.GetSafeNormal();
                const float Twist=FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(PalmProjected,Goal),ForearmAxis),FVector::DotProduct(PalmProjected,Goal));
                const float Strength=FMath::SmoothStep(0.1f,0.35f,float(DownProjected.Size()));
                Turn(Arm,FQuat(ForearmAxis,FMath::Clamp(Twist,-PI*2.f/9.f,PI*2.f/9.f)*RelaxedWeight*Strength));
            }
            const FVector Palm=PalmNormal();
            const float Bend=FMath::Acos(FMath::Clamp(FVector::DotProduct(Palm,-Up),-1.,1.));
            FVector Axis=FVector::CrossProduct(Palm,-Up).GetSafeNormal();
            if(Axis.IsNearlyZero())Axis=Right;
            Turn(Hand,FQuat(Axis,FMath::Min(Bend,PI/9.f)*RelaxedWeight));
        }
    }
}

void FRPGAnimInstanceProxy::ApplySpellPose(FPoseContext& Output) const
{
    const float TorsoWeight = FMath::Max(SpellPose[0].Weight,SpellPose[1].Weight);
    if (TorsoWeight <= 0.f) return; // Exactly preserves the approved flight and melee poses when inactive.
    FCompactPose& Pose = Output.Pose;
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    auto Find = [&Bones](FName Name)
    {
        const int32 I = Bones.GetPoseBoneIndexForBoneName(Name);
        return I==INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
    };
    const FRPGEnergyPose& Aim = SpellPose[0].Weight > SpellPose[1].Weight ? SpellPose[0] : SpellPose[1];
    const FVector Up = Aim.UpCS.GetSafeNormal(),Right = Aim.RightCS.GetSafeNormal();
    const FVector Forward = FVector::CrossProduct(Right,Up).GetSafeNormal();
    // GASP can retain root yaw from its idle/motion-matching pose. Align the whole body, rather than
    // asking either shoulder to bridge that offset. No correction exists outside spell use.
    const FCompactPoseBoneIndex Pelvis = Find(TEXT("pelvis"));
    const FCompactPoseBoneIndex ShoulderL = Find(TEXT("upperarm_l")),ShoulderR = Find(TEXT("upperarm_r"));
    if (Pelvis.IsValid() && ShoulderL.IsValid() && ShoulderR.IsValid())
    {
        const FVector CurrentRight = FVector::VectorPlaneProject(BoneComponentTransform(Pose,ShoulderR).GetLocation()-BoneComponentTransform(Pose,ShoulderL).GetLocation(),Up).GetSafeNormal();
        const float Turn = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(CurrentRight,Right),Up),FVector::DotProduct(CurrentRight,Right));
        const FTransform Parent = BoneComponentTransform(Pose,Bones.GetParentBoneIndex(Pelvis));
        const FQuat Adjust = FQuat(Right,FMath::DegreesToRadians(-Aim.HoverPitch*TorsoWeight))
            * FQuat(Forward,FMath::DegreesToRadians(Aim.HoverBank*TorsoWeight))
            * FQuat(Up,Turn*TorsoWeight);
        Pose[Pelvis].SetRotation((Parent.GetRotation().Inverse()*Adjust*Parent.GetRotation()*Pose[Pelvis].GetRotation()).GetNormalized());
    }
    const float Yaw = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Aim.DirectionCS,Right),FVector::DotProduct(Aim.DirectionCS,Forward))),-8.f,8.f);
    const float Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Aim.DirectionCS,Up),-1.,1.))),-25.f,25.f);
    for (const FSpineShare& Share : SpineShares)
    {
        const FCompactPoseBoneIndex I = Find(Share.Bone);
        if (!I.IsValid()) continue;
        const FTransform Parent = BoneComponentTransform(Pose,Bones.GetParentBoneIndex(I));
        const FQuat Turn = FQuat(Up,FMath::DegreesToRadians(Yaw*Share.Yaw*TorsoWeight)) * FQuat(Right,FMath::DegreesToRadians(-Pitch*Share.Pitch*TorsoWeight));
        Pose[I].SetRotation((Parent.GetRotation().Inverse()*Turn*Parent.GetRotation()*Pose[I].GetRotation()).GetNormalized());
    }
    for (int32 H=0;H<2;++H)
    {
        const FRPGEnergyPose& S = SpellPose[H];
        if (S.Weight <= 0.f) continue;
        const FString Side = H==0 ? TEXT("l") : TEXT("r");
        const FCompactPoseBoneIndex Root = Find(*("upperarm_"+Side)),Mid = Find(*("lowerarm_"+Side)),End = Find(*("hand_"+Side));
        if (!Root.IsValid() || !Mid.IsValid() || !End.IsValid()) continue;
        // Open palm, with the same reference finger spread on both hands.
        for (const TCHAR* Finger : {TEXT("thumb"),TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")})
            for (int32 N=1;N<=3;++N)
            {
                const FCompactPoseBoneIndex I = Find(*FString::Printf(TEXT("%s_%02d_%s"),Finger,N,*Side));
                if (I.IsValid()) Pose[I].SetRotation(FQuat::Slerp(Pose[I].GetRotation(),Bones.GetRefPoseTransform(I).GetRotation(),S.Weight).GetNormalized());
            }
        const FTransform Parent = BoneComponentTransform(Pose,Bones.GetParentBoneIndex(Root));
        const FTransform RootCS = BoneComponentTransform(Pose,Root),MidCS = BoneComponentTransform(Pose,Mid),EndCS = BoneComponentTransform(Pose,End);
        const FVector R = RootCS.GetLocation(),M = MidCS.GetLocation(),E = EndCS.GetLocation();
        const float Length = FVector::Distance(R,M)+FVector::Distance(M,E);
        const float Reach = FMath::Clamp(0.965f-0.065f*S.Charge+S.Extension*(0.015f+0.065f*S.Charge),0.89f,0.98f);
        const FVector Out = S.RightCS*(H==0?-1.f:1.f),Dir = S.DirectionCS.GetSafeNormal();
        const float Tremor = (1.f-S.Extension)*S.Charge*S.Charge*0.55f;
        const FVector Shake = S.UpCS*FMath::Sin(S.Time*49.f+H*1.1f)*Tremor + Out*FMath::Sin(S.Time*63.f)*Tremor*0.5f;
        const FVector Target = R+Dir*(Length*Reach)-S.UpCS*4.f+Out*2.f+Shake;
        const FVector Pole = R-S.UpCS*Length*0.85f+Out*Length*0.45f+Dir*Length*0.2f;
        FVector NewMid,NewEnd;
        AnimationCore::SolveTwoBoneIK(R,M,E,Pole,Target,NewMid,NewEnd,false,1.0,1.0);
        const FQuat Q1 = FQuat::FindBetweenNormals((M-R).GetSafeNormal(),(NewMid-R).GetSafeNormal());
        const FQuat Q2 = FQuat::FindBetweenNormals(Q1.RotateVector(E-M).GetSafeNormal(),(NewEnd-NewMid).GetSafeNormal());
        FTransform NewRoot = RootCS,NewMiddle = MidCS,NewHand = EndCS;
        NewRoot.SetRotation((Q1*RootCS.GetRotation()).GetNormalized());
        NewMiddle.SetRotation((Q2*Q1*MidCS.GetRotation()).GetNormalized()); NewMiddle.SetLocation(NewMid);
        NewHand.SetLocation(NewEnd);
        const FCompactPoseBoneIndex Middle = Find(*("middle_01_"+Side)),Index = Find(*("index_01_"+Side)),Pinky = Find(*("pinky_01_"+Side));
        if (Middle.IsValid() && Index.IsValid() && Pinky.IsValid())
        {
            const FVector Finger = (BoneComponentTransform(Pose,Middle).GetLocation()-E).GetSafeNormal();
            const FVector Across = (BoneComponentTransform(Pose,Index).GetLocation()-BoneComponentTransform(Pose,Pinky).GetLocation()).GetSafeNormal();
            // Volar (palm) normal, mirrored between hands. The opposite signs describe the back of the hand.
            const FVector Palm = FVector::CrossProduct(Finger,Across).GetSafeNormal()*(H==0?-1.f:1.f);
            const FQuat Face = FQuat::FindBetweenNormals(Palm,Dir);
            const FVector FingerAfter = FVector::VectorPlaneProject(Face.RotateVector(Finger),Dir).GetSafeNormal();
            const FVector FingerUp = FVector::VectorPlaneProject(S.UpCS,Dir).GetSafeNormal();
            const float Twist = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(FingerAfter,FingerUp),Dir),FVector::DotProduct(FingerAfter,FingerUp));
            NewHand.SetRotation((FQuat(Dir,Twist)*Face*EndCS.GetRotation()).GetNormalized());
        }
        else NewHand.SetRotation((Q2*Q1*EndCS.GetRotation()).GetNormalized());
        const FTransform LocalRoot = NewRoot.GetRelativeTransform(Parent),LocalMid = NewMiddle.GetRelativeTransform(NewRoot),LocalEnd = NewHand.GetRelativeTransform(NewMiddle);
        // Blend rotations only, retaining exact bone lengths throughout the transition.
        Pose[Root].SetRotation(FQuat::Slerp(Pose[Root].GetRotation(),LocalRoot.GetRotation(),S.Weight).GetNormalized());
        Pose[Mid].SetRotation(FQuat::Slerp(Pose[Mid].GetRotation(),LocalMid.GetRotation(),S.Weight).GetNormalized());
        Pose[End].SetRotation(FQuat::Slerp(Pose[End].GetRotation(),LocalEnd.GetRotation(),S.Weight).GetNormalized());
    }
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

void FRPGAnimInstanceProxy::ApplyFlight(FPoseContext& Output) const
{
    if (FlightWeight <= KINDA_SMALL_NUMBER || FlightClips.Num() != RPGFlight::NumClips) return;
    for (const UAnimSequence* Clip : FlightClips) if (!Clip) return;
    FPoseContext Base(Output), ForwardPose(Output), FastPose(Output), BrakePose(Output);
    auto Sample = [&](int32 Index, FPoseContext& Context, bool bLoop=true)
    {
        Context.ResetToRefPose();
        FAnimationPoseData Data(Context);
        const UAnimSequence* Clip = FlightClips[Index];
        const double Time = bLoop ? FlightPhase * Clip->GetPlayLength() : FMath::Clamp(FlightActionTime,0.f,Clip->GetPlayLength());
        Clip->GetAnimationPose(Data,FAnimExtractContext(Time,false));
    };
    Sample(RPGFlight::Hover,Base); Sample(RPGFlight::Forward,ForwardPose); Sample(RPGFlight::Fast,FastPose);
    FAnimationPoseData BaseData(Base), ForwardData(ForwardPose), FastData(FastPose), OutputData(Output);
    FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData,ForwardData,1.f-float(FlightBlend.X));
    FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData,FastData,1.f-float(FlightBlend.Y));
    const float TurnWeight = FMath::Abs(FlightTurn)*float(FlightBlend.X)*(1.f-float(FlightBlend.Z));
    if (TurnWeight > KINDA_SMALL_NUMBER)
    {
        FPoseContext BankPose(Output), BankFastPose(Output);
        Sample(FlightTurn < 0.f ? RPGFlight::BankL : RPGFlight::BankR,BankPose);
        Sample(FlightTurn < 0.f ? RPGFlight::BankFastL : RPGFlight::BankFastR,BankFastPose);
        FAnimationPoseData BankData(BankPose), BankFastData(BankFastPose);
        FAnimationRuntime::BlendTwoPosesTogetherInPlace(BankData,BankFastData,1.f-float(FlightBlend.Y));
        FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData,BankData,1.f-TurnWeight);
    }
    const bool bTravelAction = FlightActionIndex == RPGFlight::Accelerate || FlightActionIndex == RPGFlight::Boost;
    auto ApplyAction = [&]()
    {
        if (FlightActionWeight > KINDA_SMALL_NUMBER && FlightActionIndex >= RPGFlight::Takeoff && FlightActionIndex < RPGFlight::NumClips)
        {
            FPoseContext ActionPose(Output);
            Sample(FlightActionIndex,ActionPose,false);
            FAnimationPoseData ActionData(ActionPose);
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData,ActionData,1.f-FlightActionWeight);
        }
    };
    // Braking must respond even when acceleration or boost is still playing.
    // Takeoff and release retain their own transition priority.
    if (bTravelAction) ApplyAction();
    if (FlightBlend.Z > KINDA_SMALL_NUMBER)
    {
        Sample(RPGFlight::Brake,BrakePose);
        FAnimationPoseData BrakeData(BrakePose);
        FAnimationRuntime::BlendTwoPosesTogetherInPlace(BaseData,BrakeData,1.f-float(FlightBlend.Z));
    }
    if (!bTravelAction) ApplyAction();
    FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData,BaseData,1.f-FlightWeight);
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
		const FQuat Delta(FlowRotations[I] / Angle, Angle*(1.f-DirectionalHover.Weight));
		FTransform& Local = Pose[Index];
		Local.SetRotation((Parent.Inverse() * Delta * Parent * Local.GetRotation()).GetNormalized());
	}
}

void FRPGAnimInstanceProxy::ApplyRearSway(FPoseContext& Output) const
{
    const float Weight=1.f-FMath::Clamp(DirectionalHover.Weight,0.f,1.f);
    const FVector Rotation=RearSway.WaistRotationCS*Weight;
    const float Angle=float(Rotation.Size());
    if(Angle<1.e-5f && RearSway.KneeFlexion*Weight<1.e-5f)return;
    FCompactPose& Pose=Output.Pose;const FBoneContainer& Bones=Pose.GetBoneContainer();
    auto Find=[&Bones](FName Name)
    {
        const int32 I=Bones.GetPoseBoneIndexForBoneName(Name);
        return I==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
    };
    const auto Pelvis=Find(TEXT("pelvis")),Waist=Find(TEXT("spine_01"));
    if(Pelvis.IsValid() && Waist.IsValid() && Bones.GetParentBoneIndex(Waist)==Pelvis && Angle>1.e-5f)
    {
        const FTransform OldPelvis=BoneComponentTransform(Pose,Pelvis),OldWaist=BoneComponentTransform(Pose,Waist);
        const FTransform Parent=BoneComponentTransform(Pose,Bones.GetParentBoneIndex(Pelvis));
        const FQuat Delta(Rotation/Angle,Angle);
        FTransform NewPelvis=OldPelvis;
        // Hinge the hips around the waist joint. Its position and the entire upper body stay on the authored bank.
        NewPelvis.SetLocation(OldWaist.GetLocation()+Delta.RotateVector(OldPelvis.GetLocation()-OldWaist.GetLocation()));
        NewPelvis.SetRotation((Delta*OldPelvis.GetRotation()).GetNormalized());
        Pose[Pelvis]=NewPelvis.GetRelativeTransform(Parent);
        Pose[Waist].SetRotation((NewPelvis.GetRotation().Inverse()*OldWaist.GetRotation()).GetNormalized());
    }
    for(const TCHAR* Side:{TEXT("l"),TEXT("r")})
    {
        const FString S=Side;
        const auto Thigh=Find(*("thigh_"+S)),Calf=Find(*("calf_"+S)),Foot=Find(*("foot_"+S));
        if(!Thigh.IsValid() || !Calf.IsValid() || !Foot.IsValid())continue;
        const FVector Upper=(BoneComponentTransform(Pose,Calf).GetLocation()-BoneComponentTransform(Pose,Thigh).GetLocation()).GetSafeNormal();
        const FVector Lower=(BoneComponentTransform(Pose,Foot).GetLocation()-BoneComponentTransform(Pose,Calf).GetLocation()).GetSafeNormal();
        const FVector Hinge=FVector::CrossProduct(Upper,Lower).GetSafeNormal();
        if(Hinge.IsNearlyZero())continue;
        const FQuat Parent=BoneComponentTransform(Pose,Bones.GetParentBoneIndex(Calf)).GetRotation();
        const FQuat Delta(Hinge,RearSway.KneeFlexion*Weight);
        Pose[Calf].SetRotation((Parent.Inverse()*Delta*Parent*Pose[Calf].GetRotation()).GetNormalized());
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
