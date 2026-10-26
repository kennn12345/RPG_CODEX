#include "RPGEnergyOrb.h"
#include "RPGEnergySpellComponent.h"
#include "RPGPhysicsReactionComponent.h"
#include "RPGCharacterStatsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ARPGEnergyOrb::ARPGEnergyOrb()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); SetRootComponent(Root);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto Part=[&](const TCHAR* Name,UStaticMesh* Shape)
    {
        UStaticMeshComponent* C=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        C->SetupAttachment(Root); C->SetStaticMesh(Shape);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetGenerateOverlapEvents(false);
        C->SetCastShadow(false); C->bReceivesDecals=false; C->SetCanEverAffectNavigation(false);
        return C;
    };
    Core=Part(TEXT("Core"),Sphere.Object); Shell=Part(TEXT("Shell"),Sphere.Object);
    for(int32 I=0;I<3;++I) Filaments.Add(Part(*FString::Printf(TEXT("Orbit%d"),I),Plane.Object));
    for(int32 I=0;I<12;++I)
    {
        Sparks.Add(Part(*FString::Printf(TEXT("Spark%d"),I),Sphere.Object));
        Trail.Add(Part(*FString::Printf(TEXT("Trail%d"),I),Sphere.Object));
    }
    // Three short branching clusters, pooled once. They never become projectile/trail parts.
    for(int32 I=0;I<27;++I)
    {
        UStaticMeshComponent* Arc=Part(*FString::Printf(TEXT("ChargeStatic%d"),I),Cylinder.Object);
        Arc->SetVisibility(false); StaticArcs.Add(Arc);
    }
    InitialLifeSpan=12.f;
}

void ARPGEnergyOrb::BeginPlay()
{
    Super::BeginPlay();
    auto Material=[&](const TCHAR* Path)
    {
        UMaterialInterface* M=LoadObject<UMaterialInterface>(nullptr,Path);
        return M?UMaterialInstanceDynamic::Create(M,this):nullptr;
    };
    CoreMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyCore.M_EnergyCore"));
    ShellMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyShell.M_EnergyShell"));
    RingMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyRing.M_EnergyRing"));
    StaticMaterial=Material(TEXT("/Game/RPG/Spells/Energy/M_EnergyStatic.M_EnergyStatic"));
    if(CoreMaterial) Core->SetMaterial(0,CoreMaterial);
    if(ShellMaterial) Shell->SetMaterial(0,ShellMaterial);
    for(UStaticMeshComponent* C:Filaments) if(RingMaterial) C->SetMaterial(0,RingMaterial);
    for(UStaticMeshComponent* C:Sparks) if(CoreMaterial) C->SetMaterial(0,CoreMaterial);
    for(UStaticMeshComponent* C:Trail) if(ShellMaterial) C->SetMaterial(0,ShellMaterial);
    for(UStaticMeshComponent* C:StaticArcs) if(StaticMaterial) C->SetMaterial(0,StaticMaterial);
    StaticSeed=int32(GetUniqueID());
    TrailPositions.Init(GetActorLocation(),Trail.Num()); UpdateVisual(0.f,0.f);
}

void ARPGEnergyOrb::ChargeAt(URPGEnergySpellComponent* Source,bool bLeft)
{
    SetLifeSpan(0.f);
    ChargeSource=Source; bLeftHand=bLeft;
    if(Source)
    {
        Tint=Source->Color;
        if(ACharacter* C=Cast<ACharacter>(Source->GetOwner())) AddTickPrerequisiteComponent(C->GetMesh());
    }
}

void ARPGEnergyOrb::Launch(const FVector& Direction,float Charge,float Radius,float Speed,float Range,float Damage,float Impulse,FLinearColor Color)
{
    StopChargeEffects();
    ChargeSource.Reset(); bLaunched=true; OrbRadius=FMath::Max(1.f,Radius); Tint=Color;
    Impact.Attacker=GetOwner(); Impact.Direction=Direction.GetSafeNormal();
    Impact.Charge=FMath::Clamp(Charge,0.f,1.f); Impact.Damage=FMath::Max(0.f,Damage); Impact.Impulse=FMath::Max(0.f,Impulse);
    Velocity=Impact.Direction*FMath::Max(100.f,Speed); RemainingRange=FMath::Max(1.f,Range);
    TrailPositions.Init(GetActorLocation(),Trail.Num()); SetLifeSpan(RemainingRange/Velocity.Size()+0.5f);
    FHitResult Hit;
    const FVector Start=GetActorLocation(),End=Start+Impact.Direction*(OrbRadius+7.f);
    if(Trace(GetWorld(),Start,End,OrbRadius,GetOwner(),Hit)) HitTarget(Hit);
    else SetActorLocation(End);
}

bool ARPGEnergyOrb::Trace(UWorld* World,const FVector& Start,const FVector& End,float Radius,AActor* Ignore,FHitResult& Hit)
{
    if(!World) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(EnergySpell),false,Ignore);
    // Skip movement capsules, retaining skeletal bodies on the same actor.
    for(int32 Attempt=0;Attempt<32;++Attempt)
    {
        const bool Found=Radius>0.f?World->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Radius),Params)
            :World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Params);
        if(!Found) return false;
        if(Cast<UCapsuleComponent>(Hit.GetComponent()) && Hit.GetActor() && Hit.GetActor()->FindComponentByClass<USkeletalMeshComponent>())
        { Params.AddIgnoredComponent(Hit.GetComponent()); continue; }
        return true;
    }
    return false;
}

void ARPGEnergyOrb::ApplyImpact(const FRPGEnergyImpact& Data)
{
    AActor* Target=Data.Hit.GetActor();
    if(!IsValid(Target) || Target==Data.Attacker) return;
    if(Target->Implements<URPGEnergySpellTarget>())
    { IRPGEnergySpellTarget::Execute_ReceiveEnergyHit(Target,Data); return; }
    if(URPGCharacterStatsComponent* Stats=Target->FindComponentByClass<URPGCharacterStatsComponent>())
    {
        float Health=0.f;
        if(Stats->GetStatByName(TEXT("Health"),Health)) Stats->SetStatBaseByName(TEXT("Health"),FMath::Max(0.f,Health-Data.Damage));
    }
    else UGameplayStatics::ApplyPointDamage(Target,Data.Damage,Data.Direction,Data.Hit,Data.Attacker?Data.Attacker->GetInstigatorController():nullptr,Data.Attacker,nullptr);
    if(URPGPhysicsReactionComponent* Reaction=Target->FindComponentByClass<URPGPhysicsReactionComponent>()) Reaction->HandleEnergyHit(Data);
    else if(UPrimitiveComponent* Body=Data.Hit.GetComponent(); Body && Body->IsSimulatingPhysics(Data.Hit.BoneName))
        Body->AddImpulseAtLocation(Data.Direction*Data.Impulse,Data.Hit.ImpactPoint,Data.Hit.BoneName);
}

void ARPGEnergyOrb::HitTarget(const FHitResult& Hit)
{
    if(bImpacted) return;
    bImpacted=true; Impact.Hit=Hit; ImpactAge=0.f;
    SetActorLocation(Hit.ImpactPoint+Hit.ImpactNormal*0.8f); SetActorRotation(Hit.ImpactNormal.Rotation());
    ApplyImpact(Impact); SetLifeSpan(0.36f);
}

void ARPGEnergyOrb::Tick(float Delta)
{
    Super::Tick(Delta); Age+=Delta;
    float Charge=Impact.Charge;
    if(ChargeSource.IsValid())
    {
        const FRPGEnergyHandState& Hand=bLeftHand?ChargeSource->Left:ChargeSource->Right;
        Charge=Hand.bHeld?Hand.Charge:Hand.ReleasedCharge;
        OrbRadius=ChargeSource->GetRadius(Charge); SetActorLocation(ChargeSource->GetOrbLocation(bLeftHand));
    }
    else if(!bLaunched) { Destroy(); return; }
    else if(!bImpacted)
    {
        const FVector Start=GetActorLocation(),End=Start+(Velocity*Delta).GetClampedToMaxSize(RemainingRange);
        FHitResult Hit;
        if(Trace(GetWorld(),Start,End,OrbRadius,GetOwner(),Hit)) HitTarget(Hit); else SetActorLocation(End);
        RemainingRange-=FVector::Distance(Start,End);
        if(RemainingRange<=0.f) { Destroy(); return; }
    }
    if(bImpacted) ImpactAge+=Delta;
    UpdateVisual(Delta,Charge);
}

void ARPGEnergyOrb::UpdateVisual(float Delta,float Charge)
{
    const float Fade=bImpacted?1.f-FMath::SmoothStep(0.08f,0.35f,ImpactAge):1.f;
    const float Wave=FMath::Sin(Age*(7.f+Charge*3.f));
    const float Brightness=bImpacted?1.f:1.12f+Wave*(0.22f+Charge*0.18f);
    for(UMaterialInstanceDynamic* M:{CoreMaterial.Get(),ShellMaterial.Get(),RingMaterial.Get()})
        if(M) { M->SetVectorParameterValue(TEXT("Color"),Tint); M->SetScalarParameterValue(TEXT("Fade"),Fade); M->SetScalarParameterValue(TEXT("Brightness"),Brightness); }
    const float Pulse=1.f+(0.035f+Charge*0.025f)*Wave;
    const float Shape=bImpacted?0.f:0.025f*FMath::Sin(Age*5.3f);
    const FVector BreathingShape(1.f+Shape,1.f-Shape*0.65f,1.f-Shape*0.35f);
    const float CoreRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,0.1f,FMath::Clamp(ImpactAge/0.25f,0.f,1.f)):OrbRadius*0.72f*Pulse;
    Core->SetRelativeScale3D(BreathingShape*(CoreRadius/50.f));
    // The burst's maximum diameter is twice that of the incoming orb, without splash damage.
    const float BurstRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,2.f,FMath::SmoothStep(0.f,0.22f,ImpactAge)):OrbRadius*Pulse;
    Shell->SetRelativeScale3D(BreathingShape*(BurstRadius/50.f));
    for(int32 I=0;I<Filaments.Num();++I)
    {
        Filaments[I]->SetRelativeScale3D(FVector(BurstRadius*2.f/100.f));
        Filaments[I]->SetRelativeRotation(bImpacted?FRotator(90,I*60,0):FRotator(35+I*55,Age*(I%2?95.f:-75.f)+I*60,20+I*45));
    }
    for(int32 I=0;I<Sparks.Num();++I)
    {
        const float Phase=FMath::Fmod(Age*(0.7f+I*0.037f)+I*0.083f,1.f),Z=1.f-2.f*(I+0.5f)/Sparks.Num();
        const float A=I*2.39996f+(bImpacted?0.f:Age*1.5f);
        const FVector Radial(FMath::Sqrt(1-Z*Z)*FMath::Cos(A),FMath::Sqrt(1-Z*Z)*FMath::Sin(A),Z);
        const float Distance=bImpacted?FMath::Min(1.9f,0.6f+ImpactAge*7.f):0.95f+0.3f*Phase;
        Sparks[I]->SetRelativeLocation(Radial*OrbRadius*Distance);
        Sparks[I]->SetRelativeScale3D(FVector(OrbRadius*(bImpacted?0.055f:0.025f)*(1.f-Phase*0.5f)*Fade/50.f));
        Trail[I]->SetVisibility(bLaunched && !bImpacted);
        if(bLaunched && !bImpacted)
        {
            const FVector Follow=I==0?GetActorLocation():TrailPositions[I-1];
            TrailPositions[I]=FMath::Lerp(TrailPositions[I],Follow,1.f-FMath::Exp(-Delta*45.f));
            Trail[I]->SetWorldLocation(TrailPositions[I]);
            Trail[I]->SetWorldScale3D(FVector(OrbRadius*(1.f-float(I+1)/13.f)*0.7f/50.f));
        }
    }
    const FRPGEnergyHandState* Held=ChargeSource.IsValid()?(bLeftHand?&ChargeSource->Left:&ChargeSource->Right):nullptr;
    UpdateStatic(Delta,Held && Held->bHeld && Held->Charge>=1.f && !bLaunched && !bImpacted);
}

void ARPGEnergyOrb::StopChargeEffects()
{
    for(UStaticMeshComponent* Arc:StaticArcs) Arc->SetVisibility(false);
    StaticTime=0.f;
    for(FStaticCluster& Cluster:StaticClusters) Cluster.Cycle=INDEX_NONE;
}

void ARPGEnergyOrb::UpdateStatic(float Delta,bool bFullyHeld)
{
    if(!bFullyHeld) { StopChargeEffects(); return; }
    StaticTime+=Delta;
    if(StaticMaterial) StaticMaterial->SetVectorParameterValue(TEXT("Color"),FMath::Lerp(Tint,FLinearColor(0.65f,0.9f,1.f),0.72f));
    for(int32 I=0;I<3;++I)
    {
        FStaticCluster& Cluster=StaticClusters[I];
        const float Period=0.43f+I*0.137f,Clock=StaticTime+I*0.19f;
        const int32 Cycle=FMath::FloorToInt(Clock/Period);
        const float Phase=FMath::Fmod(Clock,Period);
        // A brief double flicker followed by a quiet gap, at independently changing surface locations.
        const bool bVisible=Phase<0.135f && !(Phase>0.043f && Phase<0.066f);
        if(bVisible && Cluster.Cycle!=Cycle)
        {
            Cluster.Cycle=Cycle; Cluster.Points.Reset(10);
            FRandomStream Random(StaticSeed+I*7919+Cycle*104729);
            const FVector Normal=Random.VRand();
            const FVector Tangent=FVector::CrossProduct(Normal,FMath::Abs(Normal.Z)<0.9f?FVector::UpVector:FVector::RightVector).GetSafeNormal();
            const FVector Across=FVector::CrossProduct(Normal,Tangent).GetSafeNormal();
            for(int32 P=0;P<7;++P)
            {
                const FVector OnSphere=Normal+Tangent*(float(P)/6.f-0.5f)*1.05f+Across*Random.FRandRange(-0.12f,0.12f);
                Cluster.Points.Add(OnSphere.GetSafeNormal()*Random.FRandRange(1.25f,1.44f));
            }
            for(int32 P=0;P<3;++P)
                Cluster.Points.Add((Cluster.Points[3]+Across*(0.14f+P*0.13f)+Tangent*Random.FRandRange(-0.09f,0.09f)).GetSafeNormal()*Random.FRandRange(1.28f,1.49f));
        }
        for(int32 S=0;S<9;++S)
        {
            UStaticMeshComponent* Arc=StaticArcs[I*9+S];
            Arc->SetVisibility(bVisible && StaticMaterial!=nullptr);
            if(!bVisible || Cluster.Points.Num()!=10) continue;
            const int32 A=S<6?S:S==6?3:S;
            const int32 B=S+1;
            const FVector Start=Cluster.Points[A]*OrbRadius,End=Cluster.Points[B]*OrbRadius;
            const FVector Segment=End-Start;
            Arc->SetRelativeLocation((Start+End)*0.5f);
            Arc->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector,Segment.GetSafeNormal()));
            const float Width=FMath::Max(0.2f,OrbRadius*0.03f)*(S<6?1.f:0.7f);
            Arc->SetRelativeScale3D(FVector(Width/100.f,Width/100.f,Segment.Size()/100.f));
        }
    }
}
