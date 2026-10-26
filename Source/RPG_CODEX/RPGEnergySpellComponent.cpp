#include "RPGEnergySpellComponent.h"
#include "RPGEnergyOrb.h"
#include "RPGAnimInstance.h"
#include "RPGCombatStateComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "RPGDirectionalHoverComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

URPGEnergySpellComponent::URPGEnergySpellComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URPGEnergySpellComponent::BeginPlay()
{
    Super::BeginPlay();
    State = GetOwner()->FindComponentByClass<URPGCombatStateComponent>();
    if (State.IsValid()) State->OnPlayerAction.AddUniqueDynamic(this, &URPGEnergySpellComponent::OnAction);
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        Mesh = Character->GetMesh();
        // GASP/CMC may choose movement-facing earlier in the frame. Spell facing owns only its active pose.
        AddTickPrerequisiteComponent(Character->GetCharacterMovement());
    }
    if (Mesh.IsValid()) Mesh->AddTickPrerequisiteComponent(this);
    AimDirection = GetOwner()->GetActorForwardVector();
}

void URPGEnergySpellComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelAll();
    if (State.IsValid()) State->OnPlayerAction.RemoveDynamic(this, &URPGEnergySpellComponent::OnAction);
    if (Mesh.IsValid())
    {
        Mesh->RemoveTickPrerequisiteComponent(this);
        if (URPGAnimInstance* Anim = Cast<URPGAnimInstance>(Mesh->GetAnimInstance()))
            for (FRPGEnergyPose& Pose : Anim->SpellPose) Pose = FRPGEnergyPose();
    }
    Super::EndPlay(Reason);
}

bool URPGEnergySpellComponent::CanCast() const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!State.IsValid() || !Character || !Character->GetController() || !Mesh.IsValid()) return false;
    // Melee and traversal keep their existing priority. Travel flight is never reshaped by this overlay.
    if (State->bIsBlocking || State->bIsChargingAttack || State->bIsChargingKick || State->AttackPhase != ERPGAttackPhase::None) return false;
    if (Character->GetCharacterMovement()->IsFalling() && !State->bIsFlying) return false;
    const auto* Hover=Character->FindComponentByClass<URPGDirectionalHoverComponent>();
    if (State->bIsFlying && Character->GetVelocity().Size()>180.f && !(Hover && Hover->IsHovering())) return false;
    if (Character->IsPlayingRootMotion()) return false;
    float Health = 1.f;
    if (URPGCharacterStatsComponent* Stats = GetOwner()->FindComponentByClass<URPGCharacterStatsComponent>())
        Stats->GetStatByName(TEXT("Health"), Health);
    return Health > 0.f;
}

void URPGEnergySpellComponent::OnAction(const FRPGPlayerActionInfo& Info)
{
    switch (Info.Action)
    {
    case ERPGPlayerAction::LeftSpell: Press(ERPGHand::Left); break;
    case ERPGPlayerAction::RightSpell: Press(ERPGHand::Right); break;
    case ERPGPlayerAction::LeftSpellReleased: Release(ERPGHand::Left); break;
    case ERPGPlayerAction::RightSpellReleased: Release(ERPGHand::Right); break;
    default: break;
    }
}

void URPGEnergySpellComponent::Press(ERPGHand Which)
{
    if (!CanCast() || (Which != ERPGHand::Left && Which != ERPGHand::Right)) return;
    FRPGEnergyHandState& Hand = Which == ERPGHand::Left ? Left : Right;
    if (Hand.bHeld || Hand.bPendingShot || Hand.ReleaseSeconds < 0.36f) return;
    Hand.bHeld = true;
    Hand.HeldSeconds = Hand.Charge = 0.f;
    Hand.ReleaseSeconds = 10.f;
    FActorSpawnParameters Spawn;
    Spawn.Owner = GetOwner(); Spawn.Instigator = Cast<APawn>(GetOwner());
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Hand.Orb = GetWorld()->SpawnActor<ARPGEnergyOrb>(GetOrbLocation(Which == ERPGHand::Left), FRotator::ZeroRotator, Spawn);
    if (Hand.Orb.IsValid()) Hand.Orb->ChargeAt(this, Which == ERPGHand::Left);
}

void URPGEnergySpellComponent::Release(ERPGHand Which)
{
    if (Which != ERPGHand::Left && Which != ERPGHand::Right) return;
    FRPGEnergyHandState& Hand = Which == ERPGHand::Left ? Left : Right;
    if (!Hand.bHeld) return;
    Hand.bHeld = false;
    if (Hand.Orb.IsValid()) Hand.Orb->StopChargeEffects();
    Hand.ReleasedCharge = Hand.Charge;
    Hand.ReleaseWait = 0.f;
    Hand.ReleaseSeconds = 0.f;
    Hand.bPendingShot = true;
}

void URPGEnergySpellComponent::CancelAll()
{
    for (FRPGEnergyHandState* Hand : {&Left,&Right})
    {
        Hand->bHeld = Hand->bPendingShot = false;
        Hand->ReleaseSeconds = 10.f;
        Hand->Charge = 0.f;
        if (Hand->Orb.IsValid()) Hand->Orb->Destroy();
        Hand->Orb.Reset();
    }
}

float URPGEnergySpellComponent::GetRadius(float Charge) const
{
    return FMath::Max(1.f,BaseRadius) * FMath::Lerp(1.f,FMath::Max(1.f,FullSizeMultiplier),FMath::Clamp(Charge,0.f,1.f));
}

bool URPGEnergySpellComponent::IsAiming() const
{
    return Left.bHeld || Right.bHeld || Left.bPendingShot || Right.bPendingShot
        || Left.ReleaseSeconds<0.32f || Right.ReleaseSeconds<0.32f;
}

FVector URPGEnergySpellComponent::GetOrbLocation(bool bLeft) const
{
    const FVector Hand = Mesh.IsValid() ? Mesh->GetSocketLocation(bLeft ? TEXT("hand_l") : TEXT("hand_r")) : GetOwner()->GetActorLocation();
    const FRPGEnergyHandState& StateForHand = bLeft ? Left : Right;
    const float Charge = StateForHand.bHeld ? StateForHand.Charge : StateForHand.ReleasedCharge;
    return Hand + AimDirection * (GetRadius(Charge) + 7.f);
}

void URPGEnergySpellComponent::UpdateAim(float Delta)
{
    APawn* Pawn = Cast<APawn>(GetOwner());
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    FVector Origin = GetOwner()->GetActorLocation() + FVector(0,0,60);
    FVector Ray = GetOwner()->GetActorForwardVector();
    if (PC)
    {
        FRotator View;
        PC->GetPlayerViewPoint(Origin,View);
        Ray = View.Vector();
        FVector CursorOrigin,CursorDirection;
        if (PC->bShowMouseCursor && PC->DeprojectMousePositionToWorld(CursorOrigin,CursorDirection))
        { Origin = CursorOrigin; Ray = CursorDirection; }
    }
    FHitResult Hit;
    AimPoint = Origin + Ray * FMath::Max(500.f,Range);
    if (ARPGEnergyOrb::Trace(GetWorld(),Origin,AimPoint,0.f,GetOwner(),Hit)) AimPoint = Hit.ImpactPoint;
    const FVector Chest = Mesh.IsValid() ? Mesh->GetSocketLocation(TEXT("spine_05")) : GetOwner()->GetActorLocation();
    const bool bActive = IsAiming();
    float YawRate = 0.f;
    if (bActive)
    {
        if (!bFacingInitialized) { FacingYaw=GetOwner()->GetActorRotation().Yaw; bFacingInitialized=true; }
        const float Goal = (AimPoint-Chest).Rotation().Yaw;
        const float Step = FMath::Clamp(FMath::FindDeltaAngleDegrees(FacingYaw,Goal)*(1.f-FMath::Exp(-Delta*10.f)),-FacingTurnRate*Delta,FacingTurnRate*Delta);
        FacingYaw = FRotator::NormalizeAxis(FacingYaw+Step);
        YawRate = Step/FMath::Max(Delta,0.001f);
        GetOwner()->SetActorRotation(FRotator(0,FacingYaw,0));
    }
    else bFacingInitialized=false;
    const FRotator Local = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(AimPoint-Chest).Rotation();
    FacingYawError = FRotator::NormalizeAxis(Local.Yaw);
    const float Yaw = FMath::Clamp(FacingYawError,-ForwardAimHalfAngle,ForwardAimHalfAngle);
    const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(Local.Pitch),-65.f,75.f);
    bAimWithinLimits = FMath::Abs(Yaw-Local.Yaw)<0.1f && FMath::Abs(Pitch-Local.Pitch)<0.1f;
    const FVector Desired = GetOwner()->GetActorTransform().TransformVectorNoScale(FRotator(Pitch,Yaw,0).Vector());
    const FVector Blended = FMath::VInterpTo(AimDirection,Desired,Delta,16.f).GetSafeNormal();
    const FRotator BlendedLocal = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(Blended).Rotation();
    // World-space smoothing must not leave a hand aiming sideways after the hips turn.
    AimDirection = GetOwner()->GetActorTransform().TransformVectorNoScale(FRotator(
        FMath::Clamp(FRotator::NormalizeAxis(BlendedLocal.Pitch),-65.f,75.f),
        FMath::Clamp(FRotator::NormalizeAxis(BlendedLocal.Yaw),-ForwardAimHalfAngle,ForwardAimHalfAngle),0).Vector());
    const bool bHover = bActive && State.IsValid() && State->bIsFlying;
    const float Alpha = 1.f-FMath::Exp(-Delta*8.f);
    HoverPitch = FMath::Lerp(HoverPitch,bHover ? FMath::Clamp(Pitch*0.18f,-11.f,13.f) : 0.f,Alpha);
    HoverBank = FMath::Lerp(HoverBank,bHover ? FMath::Clamp(-YawRate*0.025f,-6.f,6.f) : 0.f,Alpha);
    if (!bAimWithinLimits) AimPoint = Chest + Desired * FMath::Max(500.f,Range);
}

void URPGEnergySpellComponent::Fire(FRPGEnergyHandState& Hand, bool bLeft)
{
    Hand.bPendingShot = false;
    if (!Hand.Orb.IsValid()) return;
    ARPGEnergyOrb* Orb = Hand.Orb.Get();
    const FVector Palm = Mesh->GetSocketLocation(bLeft ? TEXT("hand_l") : TEXT("hand_r"));
    // A muzzle sweep from the hand also catches a wall immediately in front of the palm.
    Orb->SetActorLocation(Palm);
    FVector Direction = (AimPoint-Palm).GetSafeNormal();
    const FRotator ShotLocal = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(Direction).Rotation();
    Direction = GetOwner()->GetActorTransform().TransformVectorNoScale(FRotator(
        FMath::Clamp(FRotator::NormalizeAxis(ShotLocal.Pitch),-65.f,75.f),
        FMath::Clamp(FRotator::NormalizeAxis(ShotLocal.Yaw),-ForwardAimHalfAngle,ForwardAimHalfAngle),0).Vector());
    Orb->Launch(Direction,Hand.ReleasedCharge,GetRadius(Hand.ReleasedCharge),ProjectileSpeed,Range,
        FMath::Lerp(TapDamage,ChargedDamage,Hand.ReleasedCharge),FMath::Lerp(TapImpulse,ChargedImpulse,Hand.ReleasedCharge),Color);
    ++Hand.ShotsFired;
    Hand.Orb.Reset();
}

void URPGEnergySpellComponent::UpdateHand(FRPGEnergyHandState& Hand, bool bLeft, float Delta)
{
    if (Hand.bHeld)
    {
        Hand.HeldSeconds += Delta;
        Hand.Charge = FMath::Clamp(Hand.HeldSeconds/FMath::Max(0.1f,FullChargeSeconds),0.f,1.f);
    }
    else if (Hand.bPendingShot && (!bAimWithinLimits || Hand.PoseWeight<0.9f))
    {
        Hand.ReleaseWait += Delta;
        Hand.ReleaseSeconds = 0.f;
        if (Hand.ReleaseWait>2.f)
        {
            Hand.bPendingShot=false; Hand.ReleaseSeconds=10.f;
            if (Hand.Orb.IsValid()) Hand.Orb->Destroy();
            Hand.Orb.Reset();
        }
    }
    else Hand.ReleaseSeconds += Delta;
    if (Hand.bPendingShot && Hand.ReleaseSeconds >= 0.12f) Fire(Hand,bLeft);
    const float TargetWeight = Hand.bHeld || Hand.ReleaseSeconds < 0.32f ? 1.f : 0.f;
    Hand.PoseWeight = FMath::Lerp(Hand.PoseWeight,TargetWeight,1.f-FMath::Exp(-Delta/(TargetWeight>0.f?0.075f:0.13f)));
    if (TargetWeight == 0.f && Hand.PoseWeight < 0.001f) Hand.PoseWeight = 0.f;
    if (URPGAnimInstance* Anim = Mesh.IsValid() ? Cast<URPGAnimInstance>(Mesh->GetAnimInstance()) : nullptr)
    {
        FRPGEnergyPose& Pose = Anim->SpellPose[bLeft?0:1];
        Pose.Weight = Hand.PoseWeight;
        Pose.Charge = Hand.bHeld ? Hand.Charge : Hand.ReleasedCharge;
        // Smooth elbow extension, followed by a small recoil and a soft recovery.
        const float T = Hand.ReleaseSeconds;
        Pose.Extension = Hand.bHeld ? 0.f : T<0.14f ? FMath::SmoothStep(0.f,0.14f,T) : 1.f-0.3f*FMath::SmoothStep(0.14f,0.3f,T);
        Pose.Time = GetWorld()->GetTimeSeconds();
        Pose.HoverPitch = HoverPitch; Pose.HoverBank = HoverBank;
        Pose.DirectionCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(AimDirection);
        Pose.UpCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector);
        Pose.RightCS = Mesh->GetComponentTransform().InverseTransformVectorNoScale(GetOwner()->GetActorRightVector());
    }
}

void URPGEnergySpellComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,TickType,Tick);
    if (!CanCast()) CancelAll();
    UpdateAim(Delta);
    UpdateHand(Left,true,Delta);
    UpdateHand(Right,false,Delta);
}
