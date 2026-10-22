#include "RPGPrototypeAnimationComponent.h"
#include "RPGCombatStateComponent.h"
#include "RPGAnimInstance.h"
#include "RPGStrikeContactComponent.h"
#include "RPG_CODEX.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
    const FName PrototypeSlot(TEXT("DefaultSlot"));
    /** Layered from spine_01 up, so the legs keep the locomotion underneath. Already defined on SK_Mannequin. */
    const FName UpperBodySlot(TEXT("UpperBody"));
    /** spine_01 takes 1/3 of the layer, spine_02 2/3, spine_03 up all of it, so the waist twist between the
     *  walking hips and the punching chest is spread over the lower spine instead of one joint. */
    constexpr int32 UpperBodyBlendDepth = 3;

    /** PlaySlotAnimationAsDynamicMontage without the engine's "one montage per slot group" rule (DefaultSlot and
     *  UpperBody share a group on SK_Mannequin): whatever else is playing carries on, and the caller stops what it
     *  replaces. Lets the guard ride on top of a full-body combat stance; with the rule, starting either one blends the
     *  other out at the same time, and the idle arms-down pose shows through the crossfade (the hands dip ~20 cm). */
    UAnimMontage* PlayAlongside(UAnimInstance* Instance, UAnimSequenceBase* Clip, FName Slot, float BlendIn, float BlendOut,
        float PlayRate = 1.f, int32 LoopCount = 1, float StartAt = 0.f)
    {
        if (!Instance || !Clip) return nullptr;
        UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Clip, Slot, BlendIn, BlendOut, PlayRate, LoopCount);
        return Montage && Instance->Montage_Play(Montage, PlayRate, EMontagePlayReturnType::MontageLength, StartAt, false) > 0.f ? Montage : nullptr;
    }
}

URPGPrototypeAnimationComponent::URPGPrototypeAnimationComponent()
{
    // Ticks only while there is something to watch: a standing punch (hand-off to the upper body), a kick's movement
    // lock, a charge, or the combat stance.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    RightPunch = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_Punch_R.RPG_Proto_Punch_R"));
    LeftPunch = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_Punch_L.RPG_Proto_Punch_L"));
    RightPunchCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_Punch_R_Charged.RPG_Proto_Punch_R_Charged"));
    LeftPunchCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_Punch_L_Charged.RPG_Proto_Punch_L_Charged"));
    RightFrontKickCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FrontKick_R_Charged.RPG_Proto_FrontKick_R_Charged"));
    LeftFrontKickCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FrontKick_L_Charged.RPG_Proto_FrontKick_L_Charged"));
    CombatStance = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_CombatStance.RPG_Proto_CombatStance"));
    UnarmedGuard = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_UnarmedGuard.RPG_Proto_UnarmedGuard"));
    RightFrontKick = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FrontKick_R.RPG_Proto_FrontKick_R"));
    LeftFrontKick = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_Proto_FrontKick_L.RPG_Proto_FrontKick_L"));
    FirstPersonRightPunch = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_Punch_R.RPG_FP_Punch_R"));
    FirstPersonLeftPunch = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_Punch_L.RPG_FP_Punch_L"));
    FirstPersonRightPunchCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_Punch_R_Charged.RPG_FP_Punch_R_Charged"));
    FirstPersonLeftPunchCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_Punch_L_Charged.RPG_FP_Punch_L_Charged"));
    FirstPersonRightFrontKick = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_FrontKick_R.RPG_FP_FrontKick_R"));
    FirstPersonLeftFrontKick = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_FrontKick_L.RPG_FP_FrontKick_L"));
    FirstPersonRightFrontKickCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_FrontKick_R_Charged.RPG_FP_FrontKick_R_Charged"));
    FirstPersonLeftFrontKickCharged = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_FrontKick_L_Charged.RPG_FP_FrontKick_L_Charged"));
    FirstPersonUnarmedGuard = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_UnarmedGuard.RPG_FP_UnarmedGuard"));
    FirstPersonCombatStance = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FP_CombatStance.RPG_FP_CombatStance"));
    FlyingAttack = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FlyingAttack.RPG_FlyingAttack"));
    FlightStart = FSoftObjectPath(TEXT("/Game/RPG/Animations/Prototype/RPG_FlightStart.RPG_FlightStart"));
}

void URPGPrototypeAnimationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>())
        State->OnPlayerAction.AddUniqueDynamic(this, &URPGPrototypeAnimationComponent::HandleAction);
}

void URPGPrototypeAnimationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (URPGCombatStateComponent* State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>())
        State->OnPlayerAction.RemoveDynamic(this, &URPGPrototypeAnimationComponent::HandleAction);
    UnlockKickMovement();
    Super::EndPlay(Reason);
}

void URPGPrototypeAnimationComponent::LockMovementForKick()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    AController* Controller = Character ? Character->GetController() : nullptr;
    if (!Movement || !Controller || !Movement->IsMovingOnGround()) return;
    if (!bKickMovementLocked)
    {
        // No new acceleration; the move code keeps calling AddMovementInput, which resumes on its own after unlock.
        Controller->SetIgnoreMoveInput(true);
        bSavedSeparateBrakingFriction = Movement->bUseSeparateBrakingFriction;
        SavedBrakingFriction = Movement->BrakingFriction;
        SavedBrakingDecelerationWalking = Movement->BrakingDecelerationWalking;
        bKickMovementLocked = true;
    }
    // Constant deceleration with no friction term: the current speed reaches zero in KickStopTime, a short
    // linear bleed instead of the usual friction snap. Never brake softer than the character normally does.
    Movement->bUseSeparateBrakingFriction = true;
    Movement->BrakingFriction = 0.f;
    Movement->BrakingDecelerationWalking = FMath::Max(SavedBrakingDecelerationWalking, Movement->Velocity.Size2D() / KickStopTime);
}

void URPGPrototypeAnimationComponent::UnlockKickMovement()
{
    if (!bKickMovementLocked) return;
    bKickMovementLocked = false;
    KickMontage = nullptr;
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
    {
        Movement->bUseSeparateBrakingFriction = bSavedSeparateBrakingFriction;
        Movement->BrakingFriction = SavedBrakingFriction;
        Movement->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
    }
    if (AController* Controller = Character ? Character->GetController() : nullptr)
        Controller->SetIgnoreMoveInput(false);
}

bool URPGPrototypeAnimationComponent::IsMovingOnGround() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    // Stick input counts too, so a punch thrown on the first frame of a walk already keeps the legs.
    return Movement && Movement->IsMovingOnGround()
        && (Movement->Velocity.Size2D() > MovingPunchSpeed || !Movement->GetCurrentAcceleration().IsNearlyZero());
}

void URPGPrototypeAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UAnimInstance* Instance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;

    // Kick recovery: hand movement back as the kick starts blending to the locomotion, or at once if something
    // cut the kick short (another montage, flight, leaving the ground).
    if (bKickMovementLocked)
    {
        const FAnimMontageInstance* Kick = Instance && KickMontage ? Instance->GetActiveInstanceForMontage(KickMontage) : nullptr;
        const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
        if (!Kick || Kick->IsStopped() || !Movement || !Movement->IsMovingOnGround()
            || Kick->GetPosition() >= KickMontage->GetPlayLength() - KickRecoveryUnlockTime)
            UnlockKickMovement();
    }
    if (ChargeMontage) UpdateCharge(Instance, DeltaTime);
    if (FullBodyPunch) HandOffStandingPunch(Instance);
    if (Instance) UpdateCombatStance(Instance);
    if (Instance) UpdateGuardSteady(Instance);
    const bool bInCombat = GetWorld() && GetWorld()->GetTimeSeconds() - LastCombatActionTime < CombatStanceTimeout;
    if (!bKickMovementLocked && !FullBodyPunch && !ChargeMontage && !CombatStanceMontage && !bInCombat) SetComponentTickEnabled(false);
}

void URPGPrototypeAnimationComponent::UpdateCombatStance(UAnimInstance* Instance)
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    const bool bInCombat = GetWorld() && GetWorld()->GetTimeSeconds() - LastCombatActionTime < CombatStanceTimeout;
    const FAnimMontageInstance* Stance = CombatStanceMontage ? Instance->GetActiveInstanceForMontage(CombatStanceMontage) : nullptr;
    if (!Stance || Stance->IsStopped()) CombatStanceMontage = nullptr;
    // Out of combat (or off the ground): relax back to the normal idle.
    if (!bInCombat || !Movement || !Movement->IsMovingOnGround())
    {
        StopCombatStance(Instance, 0.4f);
        return;
    }
    // Standing still the stance plays full body (its wider, bladed feet); moving, upper body only so the legs keep the
    // locomotion (a full-body stance would freeze the legs while the capsule moves).
    const bool bWantFullBody = !IsMovingOnGround();
    // While the guard is up it covers the upper body. Under it: standing, the full-body stance keeps playing (its legs,
    // and the pose the guard blends from and back to, so the hands never pass through the idle); moving, nothing, so
    // the legs keep the locomotion. Holding the guard counts as fighting, so the stance is still up when it's let go.
    const FAnimMontageInstance* Guard = GuardMontage ? Instance->GetActiveInstanceForMontage(GuardMontage) : nullptr;
    if (Guard && !Guard->IsStopped())
    {
        LastCombatActionTime = GetWorld()->GetTimeSeconds();
        if (bWantFullBody && !CombatStanceMontage)
        {
            UAnimSequence* Sequence = LoadFor(CombatStance, Instance->CurrentSkeleton);
            if (!Sequence || Sequence->GetSkeleton() != Instance->CurrentSkeleton) return;
            // A stance still blending out (the punch the first trigger started stopped it a moment ago) is picked up
            // where it is at full weight, so the legs and the pose under the guard never sink toward the idle.
            UAnimSequence* Shown = Present(Sequence);
            const FAnimMontageInstance* Fading = nullptr;
            for (const FAnimMontageInstance* Playing : Instance->MontageInstances)
                if (Playing && Playing->Montage && Playing->IsStopped() && Playing->GetWeight() > 0.f && Playing->Montage->SlotAnimTracks.Num() > 0
                    && Playing->Montage->SlotAnimTracks[0].SlotName == PrototypeSlot && Playing->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0
                    && Playing->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() == Shown)
                    Fading = Playing;
            CombatStanceMontage = PlayAlongside(Instance, Shown, PrototypeSlot, Fading ? 0.f : 0.2f, 0.4f, 1.f, 1000, Fading ? Fading->GetPosition() : 0.f);
            bCombatStanceFullBody = true;
        }
        else if (!bWantFullBody && CombatStanceMontage && bCombatStanceFullBody) StopCombatStance(Instance, 0.2f);
        return;
    }
    if (CombatStanceMontage && bCombatStanceFullBody == bWantFullBody) return;
    // Wait until the attack / guard lets go of the body. It ends in this same stance, so the stance takes over
    // while it blends out and the guard never drops.
    const FAnimMontageInstance* Active = ActiveMontage ? Instance->GetActiveInstanceForMontage(ActiveMontage) : nullptr;
    if (Active && !Active->IsStopped()) return;
    UAnimSequence* Sequence = LoadFor(CombatStance, Instance->CurrentSkeleton);
    if (!Sequence || Sequence->GetSkeleton() != Instance->CurrentSkeleton) return;
    // Switching between the two: start the other one where this one is, so the breathing doesn't jump.
    const FAnimMontageInstance* Old = CombatStanceMontage ? Instance->GetActiveInstanceForMontage(CombatStanceMontage) : nullptr;
    const float Position = Old ? Old->GetPosition() : 0.f;
    UAnimMontage* Previous = CombatStanceMontage;
    const float Blend = Previous ? 0.15f : 0.2f;
    CombatStanceMontage = Instance->PlaySlotAnimationAsDynamicMontage(Present(Sequence), bWantFullBody ? PrototypeSlot : UpperBodySlot, Blend, 0.4f, 1.f, 1000, -1.f, Position);
    if (Previous && Previous != CombatStanceMontage) Instance->Montage_Stop(Blend, Previous);
    bCombatStanceFullBody = bWantFullBody;
}

void URPGPrototypeAnimationComponent::UpdateGuardSteady(UAnimInstance* Instance)
{
    URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Instance);
    if (!Anim) return;
    // (any instance: the active lookup drops a montage the moment it starts blending out)
    const FAnimMontageInstance* Guard = SteadyGuardMontage ? Instance->GetInstanceForMontage(SteadyGuardMontage) : nullptr;
    if (!Guard) SteadyGuardMontage = nullptr;
    const UAnimSequence* Clip = Guard && SteadyGuardMontage->SlotAnimTracks.Num() > 0 && SteadyGuardMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0
        ? Cast<UAnimSequence>(SteadyGuardMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()) : nullptr;
    const USkeleton* Skeleton = Clip ? Clip->GetSkeleton() : nullptr;
    Anim->GuardSteadyMontage = SteadyGuardMontage;
    if (!bFirstPersonPresentation || !Skeleton)
    {
        Anim->GuardSteady = 0.f;
        return;
    }
    // Where the guard clip has the upper chest at this frame (so its breathing stays), in mesh space: each bone's local
    // transform from the clip, up through its parents.
    const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
    const FAnimExtractContext Context(static_cast<double>(Guard->GetPosition()));
    FTransform Chest = FTransform::Identity;
    for (int32 Bone = Ref.FindBoneIndex(TEXT("spine_05")); Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
    {
        FTransform Local;
        Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), Context, false);
        Chest = Chest * Local;
    }
    Anim->GuardChestRest = Chest.GetTranslation();
    Anim->GuardSteady = FirstPersonGuardSteady;   // times the guard's blend weight, at evaluation
}

void URPGPrototypeAnimationComponent::StopCombatStance(UAnimInstance* Instance, float BlendTime)
{
    if (CombatStanceMontage && Instance) Instance->Montage_Stop(BlendTime, CombatStanceMontage);
    CombatStanceMontage = nullptr;
}

bool URPGPrototypeAnimationComponent::IsInCombat() const
{
    const UWorld* World = GetWorld();
    return GuardMontage || ChargeMontage || (World && World->GetTimeSeconds() - LastCombatActionTime < CombatStanceTimeout);
}

FString URPGPrototypeAnimationComponent::SetupAimAnimInstance()
{
#if WITH_EDITOR
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed"));
    if (!Blueprint) return TEXT("ERROR: ABP_Unarmed not found.");
    if (Blueprint->ParentClass == URPGAnimInstance::StaticClass()) return TEXT("ABP_Unarmed already uses RPGAnimInstance.");
    if (!Blueprint->ParentClass || !URPGAnimInstance::StaticClass()->IsChildOf(Blueprint->ParentClass))
        return FString::Printf(TEXT("ERROR: ABP_Unarmed's parent %s is not a base of RPGAnimInstance."), *GetNameSafe(Blueprint->ParentClass));
    const FString Old = GetNameSafe(Blueprint->ParentClass);
    Blueprint->ParentClass = URPGAnimInstance::StaticClass();
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UPackage* Package = Blueprint->GetOutermost();
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bSaved = UPackage::SavePackage(Package, nullptr,
        *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    return FString::Printf(TEXT("ABP_Unarmed parent %s -> RPGAnimInstance, compiled, %s."), *Old, bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
#else
    return TEXT("Editor only.");
#endif
}

UAnimSequence* URPGPrototypeAnimationComponent::LoadFor(const TSoftObjectPtr<UAnimSequence>& Clip, const USkeleton* Skeleton)
{
    UAnimSequence* Sequence = Clip.LoadSynchronous();
    if (!Skeleton || (Sequence && Sequence->GetSkeleton() == Skeleton)) return Sequence;
    const FString Name = Clip.GetAssetName();
    if (Name.IsEmpty()) return Sequence;
    const FString Path = FString::Printf(TEXT("/Game/RPG/Animations/%s/%s.%s"), *Skeleton->GetName(), *Name, *Name);
    UAnimSequence* Fitted = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    return Fitted && Fitted->GetSkeleton() == Skeleton ? Fitted : Sequence;
}

UAnimSequence* URPGPrototypeAnimationComponent::Present(UAnimSequence* Sequence) const
{
    if (!bFirstPersonPresentation || !Sequence) return Sequence;
    const TObjectPtr<UAnimSequence>* FirstPerson = FirstPersonClips.Find(Sequence);
    return FirstPerson && *FirstPerson ? FirstPerson->Get() : Sequence;
}

UAnimSequence* URPGPrototypeAnimationComponent::SourceClip(UAnimSequence* Sequence) const
{
    for (const auto& Pair : FirstPersonClips)
        if (Pair.Value == Sequence) return Pair.Key;
    return Sequence;
}

void URPGPrototypeAnimationComponent::MirrorMontagesTo(UAnimInstance* Shadow)
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    UAnimInstance* Main = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    if (!Main || !Shadow || Main == Shadow) return;
    // The same aim, as the full body shows it: bent up the spine, not swung about the eyes.
    if (const URPGAnimInstance* From = Cast<URPGAnimInstance>(Main))
        if (URPGAnimInstance* To = Cast<URPGAnimInstance>(Shadow))
        {
            To->AimYaw = From->AimYaw;
            To->AimPitch = From->AimPitch;
            To->bPitchAboutPivot = false;
        }

    TSet<UAnimMontage*> Live;
    for (const FAnimMontageInstance* Playing : Main->MontageInstances)
    {
        if (!Playing || !Playing->Montage || !Playing->IsActive()) continue;
        UAnimMontage* Montage = Playing->Montage;
        if (Montage->SlotAnimTracks.Num() == 0 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0) continue;
        Live.Add(Montage);
        const TObjectPtr<UAnimMontage>* Known = ShadowMontages.Find(Montage);
        UAnimMontage* Mirror = Known ? Known->Get() : nullptr;
        FAnimMontageInstance* Mirrored = Mirror ? Shadow->GetActiveInstanceForMontage(Mirror) : nullptr;
        if (!Mirrored)
        {
            if (Playing->IsStopped()) continue;   // already blending out: nothing to start
            const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
            UAnimSequence* Clip = SourceClip(Cast<UAnimSequence>(Segment.GetAnimReference()));
            if (!Clip) continue;
            // alongside: the visible body can have two montages of one group up (the guard over the stance), and the
            // shadow follows its stops below
            Mirror = PlayAlongside(Shadow, Clip, Montage->SlotAnimTracks[0].SlotName, Montage->BlendIn.GetBlendTime(),
                Montage->BlendOut.GetBlendTime(), Playing->GetPlayRate(), Segment.LoopingCount, Playing->GetPosition());
            if (!Mirror) continue;
            ShadowMontages.Add(Montage, Mirror);
            Mirrored = Shadow->GetActiveInstanceForMontage(Mirror);
            if (!Mirrored) continue;
        }
        // Same frame as the visible body (both clips share their length and key times), paused / stopped with it.
        if (!FMath::IsNearlyEqual(Mirrored->GetPosition(), Playing->GetPosition(), 1e-4f)) Mirrored->SetPosition(Playing->GetPosition());
        if (Playing->IsPlaying() && !Mirrored->IsPlaying()) Shadow->Montage_Resume(Mirror);
        if (!Playing->IsPlaying() && Mirrored->IsPlaying()) Shadow->Montage_Pause(Mirror);
        if (Playing->IsStopped() && !Mirrored->IsStopped()) Shadow->Montage_Stop(0.15f, Mirror);
    }
    // Anything the visible body no longer plays.
    for (auto It = ShadowMontages.CreateIterator(); It; ++It)
    {
        if (Live.Contains(It.Key())) continue;
        if (It.Value()) Shadow->Montage_Stop(0.15f, It.Value());
        It.RemoveCurrent();
    }
}

void URPGPrototypeAnimationComponent::BuildPresentationMap(const USkeleton* Skeleton)
{
    FirstPersonClips.Reset();
    const TPair<TSoftObjectPtr<UAnimSequence>*, TSoftObjectPtr<UAnimSequence>*> Pairs[] = {{&RightPunch, &FirstPersonRightPunch}, {&LeftPunch, &FirstPersonLeftPunch}, {&RightPunchCharged, &FirstPersonRightPunchCharged}, {&LeftPunchCharged, &FirstPersonLeftPunchCharged}, {&RightFrontKick, &FirstPersonRightFrontKick}, {&LeftFrontKick, &FirstPersonLeftFrontKick}, {&RightFrontKickCharged, &FirstPersonRightFrontKickCharged}, {&LeftFrontKickCharged, &FirstPersonLeftFrontKickCharged}, {&UnarmedGuard, &FirstPersonUnarmedGuard}, {&CombatStance, &FirstPersonCombatStance}};
    for (const auto& Pair : Pairs)
    {
        UAnimSequence* ThirdPerson = Pair.Key->LoadSynchronous();
        UAnimSequence* FirstPerson = Pair.Value->LoadSynchronous();
        if (ThirdPerson && FirstPerson && FirstPerson->GetSkeleton() == Skeleton) FirstPersonClips.Add(ThirdPerson, FirstPerson);
    }
}

void URPGPrototypeAnimationComponent::SetFirstPersonPresentation(bool bFirstPerson)
{
    if (bFirstPersonPresentation == bFirstPerson) return;
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UAnimInstance* Instance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    if (bFirstPerson && FirstPersonClips.Num() == 0 && Instance) BuildPresentationMap(Instance->CurrentSkeleton);
    bFirstPersonPresentation = bFirstPerson;
    if (!Instance) return;

    // Hand whatever is playing over to the other version at the same frame. Both versions share keys and length, so
    // every stored time (hold, switch, impact, hit window) stays valid. Only montages not blending out are swapped; the old
    // one blends out under its replacement.
    TObjectPtr<UAnimMontage>* Tracked[] = {&ActiveMontage, &ChargeMontage, &GuardMontage, &CombatStanceMontage, &KickMontage, &SteadyGuardMontage};
    TArray<UAnimMontage*> Swapped;
    for (TObjectPtr<UAnimMontage>* Slot : Tracked)
    {
        UAnimMontage* Old = *Slot;
        if (!Old || Swapped.Contains(Old) || Old->SlotAnimTracks.Num() == 0 || Old->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0) continue;
        Swapped.Add(Old);
        FAnimMontageInstance* Playing = Instance->GetActiveInstanceForMontage(Old);
        if (!Playing || Playing->IsStopped()) continue;
        const FAnimSegment& Segment = Old->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
        UAnimSequence* Current = Cast<UAnimSequence>(Segment.GetAnimReference());
        UAnimSequence* Want = nullptr;
        for (const auto& Pair : FirstPersonClips)
        {
            if (bFirstPerson && Pair.Key == Current) Want = Pair.Value;
            if (!bFirstPerson && Pair.Value == Current) Want = Pair.Key;
        }
        if (!Want) continue;
        const float Position = Playing->GetPosition();
        const bool bPaused = !Playing->IsPlaying();
        // alongside: swapping one of two montages that share a group (the guard over the stance) must not stop the other
        UAnimMontage* New = PlayAlongside(Instance, Want, Old->SlotAnimTracks[0].SlotName, 0.1f,
            Old->BlendOut.GetBlendTime(), 1.f, Segment.LoopingCount, Position);
        if (!New) continue;
        if (bPaused) Instance->Montage_Pause(New);
        if (const FAnimMontageInstance* Still = Instance->GetActiveInstanceForMontage(Old); Still && !Still->IsStopped())
            Instance->Montage_Stop(0.1f, Old);
        for (TObjectPtr<UAnimMontage>* Other : Tracked)
            if (*Other == Old) *Other = New;
    }
}

URPGCombatStateComponent* URPGPrototypeAnimationComponent::GetCombatState() const
{
    return GetOwner() ? GetOwner()->FindComponentByClass<URPGCombatStateComponent>() : nullptr;
}

void URPGPrototypeAnimationComponent::SetAttackPhase(ERPGAttackPhase Phase)
{
    if (URPGCombatStateComponent* State = GetCombatState())
    {
        State->AttackPhase = Phase;
        State->bAttackPhaseIsKick = Phase != ERPGAttackPhase::None && bChargeIsKick;
        if (Phase != ERPGAttackPhase::ChargeHold) State->ChargeHoldSeconds = 0.f;
    }
}

void URPGPrototypeAnimationComponent::UpdateStrikeWindow(float Position)
{
    URPGCombatStateComponent* State = GetCombatState();
    if (!State) return;
    const float Before = bChargeIsKick ? KickHitWindowBeforeImpact : PunchHitWindowBeforeImpact;
    const float After = bChargeIsKick ? KickHitWindowAfterImpact : PunchHitWindowAfterImpact;
    // A close target meets the limb before its animated impact: the contact itself opens the window, and it then stays
    // open to the usual end (so one strike is one window, one StrikeId).
    if (!bWindowOpenedByContact && Position >= ChargeImpactTime - EarlyContactLead && Position < ChargeImpactTime - Before)
        if (const URPGStrikeContactComponent* Contact = GetOwner()->FindComponentByClass<URPGStrikeContactComponent>())
            bWindowOpenedByContact = Contact->HasStrikingContact(ChargeLimb);
    const bool bOpen = (Position >= ChargeImpactTime - Before || bWindowOpenedByContact) && Position <= ChargeImpactTime + After;
    if (bOpen && !State->bStrikeHitWindowOpen) ++State->StrikeId;   // a new strike: it may hit each target once
    State->bStrikeHitWindowOpen = bOpen;
    State->StrikeLimb = ChargeLimb;
    State->bStrikeCharged = bChargeLoops;
    State->StrikeTimeFromImpact = Position - ChargeImpactTime;
}

void URPGPrototypeAnimationComponent::CloseStrikeWindow()
{
    bWindowOpenedByContact = false;
    if (URPGCombatStateComponent* State = GetCombatState()) State->bStrikeHitWindowOpen = false;
}

void URPGPrototypeAnimationComponent::ClearCharge()
{
    CloseStrikeWindow();
    ChargeMontage = nullptr;
    ChargedPlaying = nullptr;
    bChargeButtonHeld = false;
    bChargePaused = false;
    bWaitingAtLoad = false;
    SetAttackPhase(ERPGAttackPhase::None);
}

void URPGPrototypeAnimationComponent::UpdateCharge(UAnimInstance* Instance, float DeltaTime)
{
    const FAnimMontageInstance* Playing = Instance ? Instance->GetActiveInstanceForMontage(ChargeMontage) : nullptr;
    if (!Playing || Playing->IsStopped())
    {
        // Finished, or replaced by another montage (guard, the other hand, flight...).
        ClearCharge();
        return;
    }
    const float Position = Playing->GetPosition();
    URPGCombatStateComponent* State = GetCombatState();
    if (bChargePaused && !bWaitingAtLoad)
    {
        if (State) State->ChargeHoldSeconds += DeltaTime;
        return;
    }
    // Wrap a little before the loop's end: the loop starts and ends on the same resting pose, while just past its
    // end the strike has begun, so wrapping early never shows a frame of the strike.
    constexpr float LoopMargin = 0.03f;
    if (bChargeButtonHeld)
    {
        // At the punch's load key, still held: wait there until the press is longer than a tap, then carry on in the
        // Charged clip, which sinks into the cocked pose. The load key is the punch's natural rest point, so waiting
        // there reads as part of the punch, and a tap released meanwhile simply continues the tuned punch.
        if (ChargedSequence)
        {
            if (Position < ChargeSwitchTime && !bWaitingAtLoad) return;
            if (State && (bChargeIsKick ? State->bIsChargingKick : State->bIsChargingAttack))
            {
                if (bWaitingAtLoad) Instance->Montage_Resume(ChargeMontage);
                bWaitingAtLoad = false;
                bChargePaused = false;
                SwitchToChargedClip(Instance, ChargeSwitchTime);
            }
            else if (!bWaitingAtLoad)
            {
                Instance->Montage_SetPosition(ChargeMontage, ChargeSwitchTime);
                Instance->Montage_Pause(ChargeMontage);
                bChargePaused = true;
                bWaitingAtLoad = true;
            }
            return;
        }
        if (bChargeLoops)
        {
            // Charging from the switch on: first the slow rear-back to full coil, then the breathing loop there.
            if (State && State->AttackPhase == ERPGAttackPhase::ChargeHold) State->ChargeHoldSeconds += DeltaTime;
            else SetAttackPhase(ERPGAttackPhase::ChargeHold);
            if (Position < ChargedHoldStart) return;
            // Breathe in the coiled pose for as long as the button is held.
            if (Position >= ChargedHoldEnd - LoopMargin) Instance->Montage_SetPosition(ChargeMontage, ChargedHoldStart);
            return;
        }
        // Kicks (and punches without a Charged clip) stop at their hold time.
        if (Position < ChargeHoldTime) return;
        Instance->Montage_SetPosition(ChargeMontage, ChargeHoldTime);
        Instance->Montage_Pause(ChargeMontage);
        bChargePaused = true;
        SetAttackPhase(ERPGAttackPhase::ChargeHold);
        return;
    }
    // Released while still sinking into the cocked pose: skip the breathing loop when it gets there.
    if (bChargeLoops && Position >= ChargedHoldStart && Position < ChargedHoldEnd)
        Instance->Montage_SetPosition(ChargeMontage, ChargedHoldEnd);
    SetAttackPhase(Position < ChargeImpactTime ? ERPGAttackPhase::Strike : ERPGAttackPhase::Recovery);
    UpdateStrikeWindow(Position);
}

void URPGPrototypeAnimationComponent::SwitchToChargedClip(UAnimInstance* Instance, float Position)
{
    // Both clips are the same pose up to the load key, so a short crossfade just covers the swap.
    UAnimMontage* Regular = ChargeMontage;
    UAnimSequence* Charged = ChargedSequence;
    ChargedSequence = nullptr;
    UAnimMontage* Held = Instance->PlaySlotAnimationAsDynamicMontage(Present(Charged), bChargeUpperBody ? UpperBodySlot : PrototypeSlot, 0.06f, 0.18f, 1.f, 1, -1.f, Position);
    if (!Held)
    {
        ChargeHoldTime = ChargeSwitchTime;   // fall back to stopping the tuned punch at its load key
        return;
    }
    if (const FAnimMontageInstance* Old = Instance->GetActiveInstanceForMontage(Regular); Old && !Old->IsStopped())
        Instance->Montage_Stop(0.06f, Regular);
    if (ActiveMontage == Regular) ActiveMontage = Held;
    if (KickMontage == Regular) KickMontage = Held;   // the kick's movement lock follows it
    if (FullBodyPunch) FullBodyPunch = Charged;
    ChargeMontage = Held;
    ChargedPlaying = Charged;
    bChargeLoops = true;
    ChargeImpactTime = ChargedImpactTime;
}

bool URPGPrototypeAnimationComponent::ReleaseCharge(UAnimInstance* Instance, const FRPGPlayerActionInfo& Info, bool bKick)
{
    if (!ChargeMontage || bChargeIsKick != bKick || !bChargeButtonHeld || (!bKick && ChargeHand != Info.Hand)) return false;
    const FAnimMontageInstance* Playing = Instance->GetActiveInstanceForMontage(ChargeMontage);
    if (!Playing || Playing->IsStopped()) return false;
    // Same montage, same frame: the strike carries on from the loaded pose (or simply never stops, if released
    // before the attack reached it - a tap).
    bChargeButtonHeld = false;
    ChargedSequence = nullptr;   // released before the charge engaged: the tuned punch just plays on
    bWaitingAtLoad = false;
    if (bChargePaused) Instance->Montage_Resume(ChargeMontage);
    // Released in the coiled hold: jump to the end of the breathing loop (the same pose) and strike from there.
    const float Position = Playing->GetPosition();
    if (bChargeLoops && Position >= ChargedHoldStart && Position < ChargedHoldEnd)
        Instance->Montage_SetPosition(ChargeMontage, ChargedHoldEnd);
    // Released while still rearing back: crossfade from here into the strike, which starts at the loop's end. The strike
    // leaves the coil slowly (it accelerates from rest), so the short blend reads as a quick final load and release.
    else if (bChargeLoops && ChargedPlaying && Position < ChargedHoldStart)
    {
        UAnimMontage* Rearing = ChargeMontage;
        Instance->Montage_Pause(Rearing);
        if (UAnimMontage* Strike = Instance->PlaySlotAnimationAsDynamicMontage(Present(ChargedPlaying), bChargeUpperBody ? UpperBodySlot : PrototypeSlot,
                0.1f, 0.18f, 1.f, 1, -1.f, ChargedHoldEnd))
        {
            if (const FAnimMontageInstance* Old = Instance->GetActiveInstanceForMontage(Rearing); Old && !Old->IsStopped())
                Instance->Montage_Stop(0.1f, Rearing);
            if (ActiveMontage == Rearing) ActiveMontage = Strike;
            if (KickMontage == Rearing) KickMontage = Strike;
            ChargeMontage = Strike;
        }
        else
        {
            Instance->Montage_Resume(Rearing);
        }
    }
    bChargePaused = false;
    SetAttackPhase(ERPGAttackPhase::Strike);
    if (URPGCombatStateComponent* State = GetCombatState())
    {
        State->LastReleaseChargePercent = Info.ChargePercent;
        State->LastReleaseTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    }
    return true;
}

void URPGPrototypeAnimationComponent::HandOffStandingPunch(UAnimInstance* Instance)
{
    const FAnimMontageInstance* Playing = Instance && ActiveMontage ? Instance->GetActiveInstanceForMontage(ActiveMontage) : nullptr;
    if (!Playing || Playing->IsStopped())
    {
        FullBodyPunch = nullptr;
        return;
    }
    if (!IsMovingOnGround()) return;

    // Started moving during a standing punch: the legs blend back to the locomotion, the upper body carries on
    // from the same frame. The UpperBody layer reads the DefaultSlot pose, which is still this punch, so it can
    // take over without a blend. A slot in the same montage group would cut the legs' blend-out down to the
    // new blend-in, so in that case both use the same blend.
    const float Position = Playing->GetPosition();
    const USkeleton* Skeleton = Instance->CurrentSkeleton;
    const bool bSameGroup = Skeleton && Skeleton->GetSlotGroupName(UpperBodySlot) == Skeleton->GetSlotGroupName(PrototypeSlot);
    UAnimMontage* FullBody = ActiveMontage;
    Instance->Montage_Stop(0.15f, FullBody);
    ActiveMontage = Instance->PlaySlotAnimationAsDynamicMontage(Present(FullBodyPunch), UpperBodySlot, bSameGroup ? 0.15f : 0.f, 0.18f, 1.f, 1, -1.f, Position);
    FullBodyPunch = nullptr;
    // A charge in progress carries over to the upper-body montage, still held at its loaded pose if it was.
    if (ChargeMontage == FullBody)
    {
        ChargeMontage = ActiveMontage;
        bChargeUpperBody = true;
        if (ActiveMontage && bChargePaused) Instance->Montage_Pause(ActiveMontage);
    }
}

void URPGPrototypeAnimationComponent::HandleAction(const FRPGPlayerActionInfo& Info)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UAnimInstance* Instance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
    if (!Instance) return;
    if (Info.Action == ERPGPlayerAction::FlightCancelled)
    {
        if (ActiveMontage) Instance->Montage_Stop(0.15f, ActiveMontage);
        return;
    }
    // Letting go of the guard blends back to the normal stance / locomotion.
    if (Info.Action == ERPGPlayerAction::DefenseReleased)
    {
        if (GuardMontage) Instance->Montage_Stop(0.2f, GuardMontage);
        GuardMontage = nullptr;
        return;
    }
    // The second trigger turned a charging punch into the guard: drop the punch that had started loading.
    if (Info.Action == ERPGPlayerAction::AttackChargeCancelled)
    {
        if (ChargeMontage && !bChargeIsKick && ChargeHand == Info.Hand) Instance->Montage_Stop(0.1f, ChargeMontage);
        ClearCharge();
        return;
    }

    TSoftObjectPtr<UAnimSequence> Clip;
    bool bHoldUntilReleased = false;
    bool bPunch = false;
    bool bGroundedKick = false;
    bool bChargeStart = false;
    switch (Info.Action)
    {
    case ERPGPlayerAction::Attack:
    case ERPGPlayerAction::HeavyAttack:
    case ERPGPlayerAction::PowerAttack:
        // Normally the punch already started on press and is waiting at its loaded pose: let it go.
        if (ReleaseCharge(Instance, Info, false)) return;
        // This press's punch was replaced by another attack that is still being held: don't play over it.
        if (ChargeMontage && bChargeButtonHeld) return;
        // One light punch per hand for now, reused for charged attacks.
        Clip = Info.Hand == ERPGHand::Left ? LeftPunch : RightPunch;
        bPunch = true;
        break;
    case ERPGPlayerAction::AttackChargeStarted:
        Clip = Info.Hand == ERPGHand::Left ? LeftPunch : RightPunch;
        bPunch = true;
        bChargeStart = true;
        break;
    case ERPGPlayerAction::UnarmedGuard:
        Clip = UnarmedGuard;
        bHoldUntilReleased = true;
        break;
    case ERPGPlayerAction::Kick:
    case ERPGPlayerAction::HeavyKick:
    case ERPGPlayerAction::PowerKick:
        if (ReleaseCharge(Instance, Info, true)) return;
        if (ChargeMontage && bChargeButtonHeld) return;
        // Standard grounded kicks only. Blitz (sprint) kicks are meant to carry their momentum and never get here.
        Clip = Info.KickLeg == ERPGKickLeg::Left ? LeftFrontKick : RightFrontKick;
        bGroundedKick = true;
        break;
    case ERPGPlayerAction::KickChargeStarted:
        Clip = Info.KickLeg == ERPGKickLeg::Left ? LeftFrontKick : RightFrontKick;
        bGroundedKick = true;
        bChargeStart = true;
        break;
    case ERPGPlayerAction::FlyingAttack: Clip = FlyingAttack; break;
    case ERPGPlayerAction::FlightStarted: Clip = FlightStart; break;
    default: return;
    }

    UAnimSequence* Sequence = LoadFor(Clip, Instance->CurrentSkeleton);
    if (!Sequence || Sequence->GetSkeleton() != Instance->CurrentSkeleton)
    {
        UE_LOG(LogRPGInput, Warning, TEXT("Prototype clip unavailable or skeleton mismatch: %s. Run animation setup."), *Clip.ToString());
        return;
    }
    // Ignore repeated events during this clip, rather than restarting it every frame. A new press is different:
    // pressing again while the last punch / kick is still recovering starts this one straight away, so the arm goes
    // back into the load instead of the press being lost while the old attack finishes.
    UAnimSequence* Shown = Present(Sequence);   // the clip itself in third person, its first-person version in first person
    if (!bChargeStart && (Instance->IsPlayingSlotAnimation(Shown, PrototypeSlot) || Instance->IsPlayingSlotAnimation(Shown, UpperBodySlot))) return;
    // Punches, kicks and the guard keep the fighting stance up between attacks. The stance is the first frame of
    // every attack, so it hands over with the attack's own blend-in.
    if (bPunch || bGroundedKick || bHoldUntilReleased)
    {
        LastCombatActionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        // The guard goes on over a full-body stance instead (see UpdateCombatStance); an upper-body one it replaces.
        if (!bHoldUntilReleased || !bCombatStanceFullBody) StopCombatStance(Instance, bHoldUntilReleased ? 0.15f : 0.12f);
    }
    // A full-body clip freezes the legs while the capsule keeps moving, so punches thrown on the move
    // go on the upper-body layer and the locomotion keeps driving the hips and legs.
    const bool bUpperBodyPunch = bPunch && IsMovingOnGround();
    if (bHoldUntilReleased)
    {
        // The guard clip is a seamless loop of the held pose; it repeats until DefenseReleased stops it.
        // Upper body only, so the player can move while guarding. It blends in over the full-body stance, which stays;
        // anything else still playing (a punch's recovery, a cancelled charge) hands over to it as before.
        TArray<UAnimMontage*> Replaced;
        for (const FAnimMontageInstance* Playing : Instance->MontageInstances)
            if (Playing && Playing->Montage && !Playing->IsStopped() && Playing->Montage != CombatStanceMontage) Replaced.Add(Playing->Montage);
        for (UAnimMontage* Old : Replaced) Instance->Montage_Stop(0.15f, Old);
        GuardMontage = PlayAlongside(Instance, Shown, UpperBodySlot, 0.15f, 0.2f, 1.f, 1000);
        ActiveMontage = SteadyGuardMontage = GuardMontage;
        // Standing with no stance underneath (it was stopped by the punch the first trigger started): bring it back
        // under the guard now.
        UpdateCombatStance(Instance);
    }
    else
    {
        ActiveMontage = Instance->PlaySlotAnimationAsDynamicMontage(Shown, bUpperBodyPunch ? UpperBodySlot : PrototypeSlot, 0.12f, 0.18f, 1.f, 1);
    }
    // Standing punches keep the full-body footwork, but hand over to the upper body if the player starts moving.
    FullBodyPunch = bPunch && !bUpperBodyPunch && ActiveMontage ? Sequence : nullptr;
    // Kicks stay full body and stop the character (the support leg is planted) until the recovery hands
    // movement back. Anything else replacing a kick releases its lock on the next tick.
    if (bGroundedKick && ActiveMontage)
    {
        KickMontage = ActiveMontage;
        LockMovementForKick();
    }
    // Started on press: run into the loaded / chambered pose and wait there while the button is held.
    if (bChargeStart && ActiveMontage)
    {
        const bool bLeft = bPunch ? Info.Hand == ERPGHand::Left : Info.KickLeg == ERPGKickLeg::Left;
        ChargeMontage = ActiveMontage;
        bChargeIsKick = !bPunch;
        ChargeHand = bPunch ? Info.Hand : ERPGHand::None;
        ChargeLimb = bPunch ? (bLeft ? ERPGStrikeLimb::LeftHand : ERPGStrikeLimb::RightHand)
                            : (bLeft ? ERPGStrikeLimb::LeftFoot : ERPGStrikeLimb::RightFoot);
        CloseStrikeWindow();   // a new press cuts off the last strike's window
        bChargeUpperBody = bUpperBodyPunch;
        bChargeLoops = false;
        bWaitingAtLoad = false;
        ChargedSequence = nullptr;
        ChargeImpactTime = bPunch ? (bLeft ? LeftPunchImpactTime : RightPunchImpactTime) : (bLeft ? LeftKickImpactTime : RightKickImpactTime);
        if (bPunch)
        {
            ChargeSwitchTime = bLeft ? LeftPunchChargeSwitchTime : RightPunchChargeSwitchTime;
            ChargedHoldStart = bLeft ? LeftPunchHoldStart : RightPunchHoldStart;
            ChargedHoldEnd = bLeft ? LeftPunchHoldEnd : RightPunchHoldEnd;
            ChargedImpactTime = bLeft ? LeftPunchChargedImpactTime : RightPunchChargedImpactTime;
            ChargedSequence = LoadFor(bLeft ? LeftPunchCharged : RightPunchCharged, Instance->CurrentSkeleton);
        }
        else
        {
            // Kicks: no rear-back, the breathing loop starts right at the hold frame.
            ChargeSwitchTime = ChargedHoldStart = bLeft ? LeftKickHoldTime : RightKickHoldTime;
            ChargedHoldEnd = bLeft ? LeftKickHoldEnd : RightKickHoldEnd;
            ChargedImpactTime = bLeft ? LeftKickChargedImpactTime : RightKickChargedImpactTime;
            ChargedSequence = LoadFor(bLeft ? LeftFrontKickCharged : RightFrontKickCharged, Instance->CurrentSkeleton);
        }
        if (ChargedSequence && ChargedSequence->GetSkeleton() != Instance->CurrentSkeleton) ChargedSequence = nullptr;
        // Without a Charged clip, stop the regular clip at its switch time instead.
        ChargeHoldTime = ChargedSequence ? ChargedHoldStart : ChargeSwitchTime;
        bChargeButtonHeld = true;
        bChargePaused = false;
        SetAttackPhase(ERPGAttackPhase::Windup);
    }
    SetComponentTickEnabled(true);   // charge / hand-off / kick lock / combat stance each switch it off when idle
    UE_LOG(LogRPGInput, Log, TEXT("Prototype montage %s: %s"), ActiveMontage ? TEXT("started") : TEXT("failed"), *Sequence->GetName());
}

FString URPGPrototypeAnimationComponent::PreparePrototypeAnimations()
{
#if WITH_EDITOR
    USkeleton* Target = LoadObject<USkeleton>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
    USkeleton* Source = LoadObject<USkeleton>(nullptr, TEXT("/Game/FreeAnimationsPack/Demo/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed"));
    if (!Target || !Source || !Blueprint) return TEXT("ERROR: expected skeleton or animation blueprint is missing.");

    // Only reuse bone-space animation if all raw bones, parents and reference poses match.
    const FReferenceSkeleton& A = Source->GetReferenceSkeleton();
    const FReferenceSkeleton& B = Target->GetReferenceSkeleton();
    if (A.GetRawBoneNum() != B.GetRawBoneNum()) return TEXT("ERROR: skeleton bone counts differ; full retargeting is required.");
    for (int32 I = 0; I < A.GetRawBoneNum(); ++I)
    {
        if (A.GetBoneName(I) != B.GetBoneName(I) || A.GetParentIndex(I) != B.GetParentIndex(I)
            || !A.GetRawRefBonePose()[I].Equals(B.GetRawRefBonePose()[I], 0.01f))
            return FString::Printf(TEXT("ERROR: skeleton reference mismatch at %s; full retargeting is required."), *A.GetBoneName(I).ToString());
    }

    // Confirm a montage slot is on a connected path to the main animation output.
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    bool bConnectedSlot = false;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph->GetFName() != TEXT("AnimGraph")) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Cast<UAnimGraphNode_Root>(Node)) continue;
            TArray<UEdGraphNode*> Pending {Node};
            TSet<UEdGraphNode*> Seen;
            while (!Pending.IsEmpty())
            {
                UEdGraphNode* Current = Pending.Pop();
                if (Seen.Contains(Current)) continue;
                Seen.Add(Current);
                if (UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Current))
                    bConnectedSlot |= Slot->Node.SlotName == PrototypeSlot;
                for (UEdGraphPin* Pin : Current->Pins)
                    if (Pin && Pin->Direction == EGPD_Input)
                        for (UEdGraphPin* Link : Pin->LinkedTo)
                            if (Link) Pending.Add(Link->GetOwningNode());
            }
        }
    }
    if (!bConnectedSlot) return TEXT("ERROR: DefaultSlot is not connected to AnimGraph output. No existing blueprint was changed.");

    struct FClip { const TCHAR* SourceName; const TCHAR* TargetName; };
    const FClip Clips[] = {
        {TEXT("AS_ComboHands"), TEXT("RPG_ComboHands")},
        {TEXT("AS_RightLegKick2"), TEXT("RPG_RightLegKick")},
        {TEXT("AS_AttackInLevitation2"), TEXT("RPG_FlyingAttack")},
        {TEXT("AS_FlyingUp"), TEXT("RPG_FlightStart")}
    };
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    FString Report = TEXT("Skeleton bone names, hierarchy and reference poses match. Connected DefaultSlot verified.\n");
    for (const FClip& Clip : Clips)
    {
        const FString SourcePath = FString::Printf(TEXT("/Game/FreeAnimationsPack/Animations/%s.%s"), Clip.SourceName, Clip.SourceName);
        const FString TargetPath = FString::Printf(TEXT("/Game/RPG/Animations/Prototype/%s.%s"), Clip.TargetName, Clip.TargetName);
        UAnimSequence* Original = LoadObject<UAnimSequence>(nullptr, *SourcePath);
        if (!Original) return Report + TEXT("ERROR: missing source ") + SourcePath;
        UAnimSequence* Copy = LoadObject<UAnimSequence>(nullptr, *TargetPath);
        if (Copy)
        {
            if (Copy->GetSkeleton() != Target) return Report + TEXT("ERROR: existing destination has unexpected skeleton: ") + TargetPath;
            Report += TEXT("Already prepared: ") + TargetPath + TEXT("\n");
            continue;
        }
        Copy = Cast<UAnimSequence>(Tools.DuplicateAsset(Clip.TargetName, TEXT("/Game/RPG/Animations/Prototype"), Original));
        if (!Copy || !Copy->ReplaceSkeleton(Target, false)) return Report + TEXT("ERROR: unable to adapt ") + SourcePath;
        // Existing movement code controls displacement. Keep these prototype clips in place.
        Copy->bEnableRootMotion = false;
        Copy->bForceRootLock = true;
        Copy->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        UPackage* Package = Copy->GetOutermost();
        if (!UPackage::SavePackage(Package, Copy, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args))
            return Report + TEXT("ERROR: saving failed: ") + TargetPath;
        Report += TEXT("Prepared: ") + TargetPath + TEXT("\n");
    }
    return Report + TEXT("SUCCESS: four prototype clips prepared; existing animation blueprint and original pack preserved.");
#else
    return TEXT("ERROR: animation setup requires an editor build.");
#endif
}

bool URPGPrototypeAnimationComponent::SetClipSampleRate(UAnimSequence* Sequence, int32 FramesPerSecond)
{
#if WITH_EDITOR
    FStructProperty* Property = FindFProperty<FStructProperty>(UAnimSequence::StaticClass(), TEXT("PlatformTargetFrameRate"));
    if (!Sequence || FramesPerSecond <= 0 || !Property || Property->Struct != FPerPlatformFrameRate::StaticStruct()) return false;
    // Same path as editing the property in the details panel: PostEditChangeProperty resamples and recompresses.
    Sequence->Modify();
    Sequence->PreEditChange(Property);
    *Property->ContainerPtrToValuePtr<FPerPlatformFrameRate>(Sequence) = FPerPlatformFrameRate(FFrameRate(FramesPerSecond, 1));
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
    Sequence->PostEditChangeProperty(Event);
    return Sequence->GetSamplingFrameRate() == FFrameRate(FramesPerSecond, 1);
#else
    return false;
#endif
}

FString URPGPrototypeAnimationComponent::SetupUpperBodySlot()
{
#if WITH_EDITOR
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed"));
    if (!Blueprint || !Blueprint->TargetSkeleton) return TEXT("ERROR: ABP_Unarmed or its skeleton is missing.");
    if (!Blueprint->TargetSkeleton->ContainsSlotName(UpperBodySlot)) return TEXT("ERROR: skeleton has no UpperBody slot.");

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph* const* Found = Graphs.FindByPredicate([](const UEdGraph* G) { return G->GetFName() == TEXT("AnimGraph"); });
    if (!Found) return TEXT("ERROR: AnimGraph not found.");
    UEdGraph* Graph = *Found;

    auto CompileAndSave = [Blueprint]() -> bool
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error) return false;
        UPackage* Package = Blueprint->GetOutermost();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Package, Blueprint, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    };
    FBranchFilter Filter;
    Filter.BoneName = TEXT("spine_01");
    Filter.BlendDepth = UpperBodyBlendDepth;

    // Find the existing DefaultSlot. If the upper-body layer is already there, only bring its mask up to date.
    UAnimGraphNode_Slot* DefaultSlotNode = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node);
        if (!Slot) continue;
        if (Slot->Node.SlotName == PrototypeSlot) DefaultSlotNode = Slot;
        if (Slot->Node.SlotName != UpperBodySlot) continue;
        const UEdGraphPin* Out = Slot->FindPin(TEXT("Pose"), EGPD_Output);
        UAnimGraphNode_LayeredBoneBlend* Existing = Out && Out->LinkedTo.Num() == 1 ? Cast<UAnimGraphNode_LayeredBoneBlend>(Out->LinkedTo[0]->GetOwningNode()) : nullptr;
        if (!Existing || Existing->Node.LayerSetup.Num() < 1) return TEXT("ERROR: UpperBody slot does not feed a layered blend. Nothing changed.");
        TArray<FBranchFilter>& Filters = Existing->Node.LayerSetup[0].BranchFilters;
        if (Filters.Num() == 1 && Filters[0].BoneName == Filter.BoneName && Filters[0].BlendDepth == Filter.BlendDepth)
            return TEXT("Already set up: ABP_Unarmed has an UpperBody slot with the current mask.");
        Existing->Modify();
        Filters = {Filter};
        if (!CompileAndSave()) return TEXT("ERROR: ABP_Unarmed failed to compile or save after the mask update.");
        return FString::Printf(TEXT("SUCCESS: UpperBody layer mask updated (spine_01, blend depth %d). ABP_Unarmed compiled and saved."), UpperBodyBlendDepth);
    }
    if (!DefaultSlotNode) return TEXT("ERROR: DefaultSlot node not found.");
    UEdGraphPin* SlotOut = DefaultSlotNode->FindPin(TEXT("Pose"), EGPD_Output);
    if (!SlotOut || SlotOut->LinkedTo.Num() != 1) return TEXT("ERROR: DefaultSlot output is not connected to exactly one node. Nothing changed.");
    UEdGraphPin* Downstream = SlotOut->LinkedTo[0];

    // DefaultSlot -> [cache] -> Layered blend per bone (base = cache, layer 0 = UpperBody slot over the cache) -> downstream.
    const int32 X = DefaultSlotNode->NodePosX, Y = DefaultSlotNode->NodePosY;
    auto Place = [](UEdGraphNode* Node, int32 PosX, int32 PosY) { Node->NodePosX = PosX; Node->NodePosY = PosY; };

    FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> SaveCreator(*Graph);
    UAnimGraphNode_SaveCachedPose* Save = SaveCreator.CreateNode(false);
    SaveCreator.Finalize();
    Save->CacheName = TEXT("RPG_BeforeUpperBody");
    Place(Save, X + 300, Y + 200);

    auto MakeUse = [&](int32 PosY)
    {
        FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph);
        UAnimGraphNode_UseCachedPose* Use = Creator.CreateNode(false);
        Use->SaveCachedPoseNode = Save;
        Creator.Finalize();
        Place(Use, X + 600, PosY);
        return Use;
    };
    UAnimGraphNode_UseCachedPose* UseForBase = MakeUse(Y);
    UAnimGraphNode_UseCachedPose* UseForSlot = MakeUse(Y + 150);

    FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*Graph);
    UAnimGraphNode_Slot* UpperSlot = SlotCreator.CreateNode(false);
    UpperSlot->Node.SlotName = UpperBodySlot;
    SlotCreator.Finalize();
    Place(UpperSlot, X + 850, Y + 150);

    FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> BlendCreator(*Graph);
    UAnimGraphNode_LayeredBoneBlend* Blend = BlendCreator.CreateNode(false);
    BlendCreator.Finalize();
    if (Blend->Node.LayerSetup.Num() < 1) return TEXT("ERROR: layered blend node has no layer. Nothing saved.");
    Blend->Node.LayerSetup[0].BranchFilters.Add(Filter);
    // Keep the guard's torso/arm orientation in mesh space, independent of the walking pelvis underneath.
    Blend->Node.bMeshSpaceRotationBlend = true;
    Place(Blend, X + 1150, Y);

    const UEdGraphSchema* Schema = Graph->GetSchema();
    SlotOut->BreakLinkTo(Downstream);
    const bool bLinked =
        Schema->TryCreateConnection(SlotOut, Save->FindPin(TEXT("Pose"), EGPD_Input)) &&
        Schema->TryCreateConnection(UseForBase->FindPin(TEXT("Pose"), EGPD_Output), Blend->FindPin(TEXT("BasePose"), EGPD_Input)) &&
        Schema->TryCreateConnection(UseForSlot->FindPin(TEXT("Pose"), EGPD_Output), UpperSlot->FindPin(TEXT("Source"), EGPD_Input)) &&
        Schema->TryCreateConnection(UpperSlot->FindPin(TEXT("Pose"), EGPD_Output), Blend->FindPin(TEXT("BlendPoses_0"), EGPD_Input)) &&
        Schema->TryCreateConnection(Blend->FindPin(TEXT("Pose"), EGPD_Output), Downstream);
    if (!bLinked) return TEXT("ERROR: could not wire the upper-body layer. Not saved; restore from backup if the asset was touched.");

    if (!CompileAndSave()) return TEXT("ERROR: ABP_Unarmed failed to compile or save after the change.");
    return FString::Printf(TEXT("SUCCESS: UpperBody slot layered from spine_01 inserted after DefaultSlot (feeds %s). ABP_Unarmed compiled and saved."),
        *Downstream->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString());
#else
    return TEXT("ERROR: animation setup requires an editor build.");
#endif
}
