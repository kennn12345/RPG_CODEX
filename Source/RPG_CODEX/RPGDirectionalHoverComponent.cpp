#include "RPGDirectionalHoverComponent.h"
#include "RPGAnimInstance.h"
#include "RPGFlightAnimationComponent.h"
#include "RPGMovementAbilityComponent.h"
#include "RPGEnergySpellComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"

URPGDirectionalHoverComponent::URPGDirectionalHoverComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void URPGDirectionalHoverComponent::BeginPlay()
{
    Super::BeginPlay();
    MoveAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move.IA_Move"));
    WorldMoveAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Input/IA_Move_WorldSpace.IA_Move_WorldSpace"));
    if(auto* C=Cast<ACharacter>(GetOwner()))
    {
        AddTickPrerequisiteComponent(C->GetCharacterMovement());
        if(auto* Flight=C->FindComponentByClass<URPGFlightAnimationComponent>()) AddTickPrerequisiteComponent(Flight);
        C->GetMesh()->AddTickPrerequisiteComponent(this);
        if(auto* Spells=C->FindComponentByClass<URPGEnergySpellComponent>())Spells->AddTickPrerequisiteComponent(this);
    }
}
void URPGDirectionalHoverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* C=Cast<ACharacter>(GetOwner()))
    {
        if(auto* Anim=Cast<URPGAnimInstance>(C->GetMesh()->GetAnimInstance()))Anim->DirectionalHover=FRPGDirectionalHoverPose();
        C->GetMesh()->RemoveTickPrerequisiteComponent(this);
        if(auto* Spells=C->FindComponentByClass<URPGEnergySpellComponent>())Spells->RemoveTickPrerequisiteComponent(this);
    }
    Super::EndPlay(Reason);
}
void URPGDirectionalHoverComponent::UpdateIntent()
{
    bFacingHandoff=false;
    auto* C=Cast<ACharacter>(GetOwner());
    auto* Move=C?C->GetCharacterMovement():nullptr;
    if(!C || !Move || !Move->IsFlying() || !C->GetController() || C->IsPlayingRootMotion())
    { bHoverRequested=false;return; }
    const auto* Ability=C->FindComponentByClass<URPGMovementAbilityComponent>();
    // Explicit vertical travel keeps its approved flight presentation and rise controls.
    if((Ability && Ability->IsFlightRiseHeld()) || FMath::Abs(Move->Velocity.Z)>180.f)
    { bHoverRequested=false;return; }
    FVector Input=C->GetPendingMovementInputVector();
    if(Input.IsNearlyZero())Input=C->GetLastMovementInputVector();
    const FRotator View(0,C->GetController()->GetControlRotation().Yaw,0);
    float Magnitude=Input.Size2D();const float Speed=Move->Velocity.Size2D();
    // GASP normalizes AddMovementInput. Keep analog hover/travel intent from the original action value.
    if(const auto* PC=Cast<APlayerController>(C->GetController()))
        if(const auto* EI=Cast<UEnhancedPlayerInput>(PC->PlayerInput);EI && MoveAction)
            Magnitude=FMath::Max(EI->GetActionValue(MoveAction).GetMagnitude(),WorldMoveAction?EI->GetActionValue(WorldMoveAction).GetMagnitude():0.f);
    const float Forward=FVector::DotProduct(Input.GetSafeNormal2D(),View.Vector());
    const auto* Spells=C->FindComponentByClass<URPGEnergySpellComponent>();
    const bool bCasting=Spells && Spells->IsAiming();
    const bool bTravel=Magnitude>0.65f && Forward>0.86f && !bCasting;
    // Hold view-facing intent through the low-speed brake-to-hover gap, before its upright pose takes over.
    bFacingHandoff=!bTravel && Speed<FMath::Max(HoverSpeed,50.f)*1.3f;
    // Preserve fast travel and air evades. Entry into directional hover requires low momentum.
    if(Speed>FMath::Max(HoverSpeed,50.f)*1.8f) { bHoverRequested=false;return; }
    if(!bHoverRequested && Speed>250.f)return;
    if(bTravel)
    {
        // Finish a backward/sideward hover reversal before returning to prone forward flight.
        bHoverRequested=bHoverRequested && FVector::DotProduct(Move->Velocity,View.Vector())<80.f;
        return;
    }
    if(Magnitude<0.05f && !bHoverRequested)
    {
        const auto* Flight=C->FindComponentByClass<URPGFlightAnimationComponent>();
        if(Speed>60.f || (Flight && Flight->PoseBlend.Z>0.1f))return;
    }
    bHoverRequested=true;
}
void URPGDirectionalHoverComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick);
    auto* C=Cast<ACharacter>(GetOwner());
    if(!C)return;
    auto* Move=C->GetCharacterMovement();auto* Mesh=C->GetMesh();
    auto* Anim=Cast<URPGAnimInstance>(Mesh->GetAnimInstance());
    if(!Anim)return;
    if(!Move->IsFlying() || C->IsPlayingRootMotion())bHoverRequested=false;
    const bool bActive=bHoverRequested && Move->IsFlying();
    const float Dt=FMath::Min(Delta,0.1f);
    const float SpeedRef=FMath::Max(HoverSpeed,50.f);
    if(bActive)
    {
        const FVector Flat=FVector(Move->Velocity.X,Move->Velocity.Y,0).GetClampedToMaxSize(SpeedRef);
        Move->Velocity.X=Flat.X;Move->Velocity.Y=Flat.Y;
    }
    const auto* Spells=C->FindComponentByClass<URPGEnergySpellComponent>();
    const bool bCasting=Spells && Spells->IsAiming();
    float YawRate=0.f;
    if(WantsHoverFacing() && Move->IsFlying())
    {
        if(!bHaveFacing) { FacingYaw=C->GetActorRotation().Yaw;FacingVelocity=0.f;bHaveFacing=true; }
        const float Goal=C->GetController()?C->GetController()->GetControlRotation().Yaw:FacingYaw;
        const float Error=FMath::FindDeltaAngleDegrees(FacingYaw,Goal);
        const float MaxRate=bCasting?300.f:HoverTurnSpeed;
        const float WantVelocity=FMath::Clamp(Error/0.22f,-MaxRate,MaxRate);
        FacingVelocity=FMath::FInterpConstantTo(FacingVelocity,WantVelocity,Dt,bCasting?HoverTurnAcceleration*2.f:HoverTurnAcceleration);
        float Step=FacingVelocity*Dt;
        if(Step*Error>0.f && FMath::Abs(Step)>FMath::Abs(Error)) { Step=Error;FacingVelocity=0.f; }
        YawRate=Step/FMath::Max(Dt,.001f);
        FacingYaw=FRotator::NormalizeAxis(FacingYaw+Step);C->SetActorRotation(FRotator(0,FacingYaw,0));
    }
    else { bHaveFacing=false;FacingVelocity=0.f; }
    const bool bYield=C->IsPlayingRootMotion();
    const float Target=bActive && !bYield?1.f:0.f;
    PoseWeight=FMath::Lerp(PoseWeight,Target,1.f-FMath::Exp(-Dt/FMath::Max(PoseResponse,0.05f)));
    if(Target==0.f && PoseWeight<0.001f)PoseWeight=0.f;
    const FVector Vel=C->GetActorTransform().InverseTransformVectorNoScale(Move->Velocity);
    const FVector Accel=bHaveVelocity?(Move->Velocity-LastVelocity)/FMath::Max(Delta,0.001f):FVector::ZeroVector;
    LastVelocity=Move->Velocity;bHaveVelocity=true;
    const FVector LocalAccel=C->GetActorTransform().InverseTransformVectorNoScale(Accel);
    const FVector2D Want=bActive?FVector2D(
        FMath::Clamp(float(Vel.X/SpeedRef+LocalAccel.X/2000.f*0.25f),-1.f,1.f),
        FMath::Clamp(float(Vel.Y/SpeedRef+LocalAccel.Y/2000.f*0.25f),-1.f,1.f)):FVector2D::ZeroVector;
    DirectionBlend=FMath::Lerp(DirectionBlend,Want,1.f-FMath::Exp(-Dt/0.18f));
    // Arms catch the lateral lean a little after the hips, retaining a soft trailing response on reversals.
    ArmSideBlend=FMath::Lerp(ArmSideBlend,float(DirectionBlend.Y),1.f-FMath::Exp(-Dt/FMath::Max(SideArmResponse,.05f)));
    if(FMath::Abs(DirectionBlend.Y)<.001 && FMath::Abs(ArmSideBlend)<.001f)ArmSideBlend=0.f;
    for(int32 Side=0;Side<2;++Side)
    {
        const float Lead=FMath::SmoothStep(0.1f,0.8f,float(DirectionBlend.Y)*(Side==0?-1.f:1.f));
        const float OutTarget=bActive?FMath::SmoothStep(.72f,1.f,Lead):0.f;
        const float Response=OutTarget>KneeOutBlend[Side]?FMath::Max(KneeDriftTime,.1f):.22f;
        KneeOutBlend[Side]=FMath::Lerp(KneeOutBlend[Side],double(OutTarget),double(1.f-FMath::Exp(-Dt/Response)));
        if(OutTarget==0.f && KneeOutBlend[Side]<.001)KneeOutBlend[Side]=0.;
        // The foot gathers inward only after the hip has opened. An analytical critically-damped
        // response retains velocity across target changes without overshooting or popping.
        const double FootTarget=bActive?FMath::SmoothStep(.65,.95,KneeOutBlend[Side]):0.;
        const double Omega=4./FMath::Max(FootTuckResponse,.1f),E=FMath::Exp(-Omega*Dt);
        const double Offset=FootTuckBlend[Side]-FootTarget;
        const double Temp=(FootTuckVelocity[Side]+Omega*Offset)*Dt;
        FootTuckBlend[Side]=FMath::Clamp(FootTarget+(Offset+Temp)*E,0.,1.);
        FootTuckVelocity[Side]=(FootTuckVelocity[Side]-Omega*Temp)*E;
        if(FootTarget==0. && FootTuckBlend[Side]<.0001 && FMath::Abs(FootTuckVelocity[Side])<.001)
            FootTuckBlend[Side]=FootTuckVelocity[Side]=0.;
    }
    const float TurnTarget=bActive && !bCasting?FMath::Clamp(YawRate/FMath::Max(HoverTurnSpeed,30.f),-1.f,1.f)
        *(1.f-FMath::Clamp(float(Move->Velocity.Size2D())/150.f,0.f,1.f)):0.f;
    TurnBlend=FMath::Lerp(TurnBlend,TurnTarget,1.f-FMath::Exp(-Dt/.16f));
    const bool bStill=bActive && Move->Velocity.Size()<5.f
        && C->GetPendingMovementInputVector().Size()<0.05f && C->GetLastMovementInputVector().Size()<0.05f;
    BobWeight=FMath::Lerp(BobWeight,bStill?1.f:0.f,1.f-FMath::Exp(-Dt/(bStill?0.45f:0.12f)));
    if(!bStill && BobWeight<0.001f)BobWeight=0.f;
    Phase=FMath::Fmod(Phase+Dt/FMath::Max(IdleBobPeriod,1.f),1.f);
    auto& Pose=Anim->DirectionalHover;
    Pose.Weight=PoseWeight*Anim->FlightWeight;
    // Keep the approved takeoff and stopping brace visible before the new idle layer settles in.
    if(Anim->FlightActionIndex==RPGFlight::Takeoff)Pose.Weight*=1.f-Anim->FlightActionWeight;
    if(bStill)Pose.Weight*=1.f-FMath::Clamp(float(Anim->FlightBlend.Z),0.f,1.f);
    Pose.Forward=DirectionBlend.X;Pose.Right=DirectionBlend.Y;
    Pose.Bob=IdleBobAmplitude*FMath::Sin(Phase*2.f*PI)*BobWeight;
    Pose.SideKneeLift=SideKneeLift;Pose.SideKneeBend=SideKneeBend;
    Pose.SideKneeOut=SideKneeOut;Pose.KneeOut=KneeOutBlend;Pose.Turn=TurnBlend;
    Pose.SideHipOpen=SideHipOpen;Pose.TrailingArmMinOut=TrailingArmMinOut;
    Pose.TrailingArmSpacing=TrailingArmSpacing;
    Pose.SideFootTuck=SideFootTuck;
    for(int32 Side=0;Side<2;++Side)
        Pose.FootTuck[Side]=FootTuckBlend[Side]*FMath::SmoothStep(.35,.8,KneeOutBlend[Side])
            *FMath::SmoothStep(.1f,.8f,float(DirectionBlend.Y)*(Side==0?-1.f:1.f));
    Pose.SideBodyLean=SideBodyLean;Pose.SideChestCounterLean=SideChestCounterLean;
    Pose.SideArmLean=SideArmLean;Pose.ArmSide=ArmSideBlend;
    Pose.UpCS=Mesh->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector);
    Pose.RightCS=Mesh->GetComponentTransform().InverseTransformVectorNoScale(C->GetActorRightVector());
}
