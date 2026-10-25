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
    if(CoreMaterial) Core->SetMaterial(0,CoreMaterial);
    if(ShellMaterial) Shell->SetMaterial(0,ShellMaterial);
    for(UStaticMeshComponent* C:Filaments) if(RingMaterial) C->SetMaterial(0,RingMaterial);
    for(UStaticMeshComponent* C:Sparks) if(CoreMaterial) C->SetMaterial(0,CoreMaterial);
    for(UStaticMeshComponent* C:Trail) if(ShellMaterial) C->SetMaterial(0,ShellMaterial);
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
    for(UMaterialInstanceDynamic* M:{CoreMaterial.Get(),ShellMaterial.Get(),RingMaterial.Get()})
        if(M) { M->SetVectorParameterValue(TEXT("Color"),Tint); M->SetScalarParameterValue(TEXT("Fade"),Fade); }
    const float Pulse=1.f+0.025f*FMath::Sin(Age*(12.f+Charge*10.f));
    const float CoreRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,0.1f,FMath::Clamp(ImpactAge/0.25f,0.f,1.f)):OrbRadius*0.72f*Pulse;
    Core->SetRelativeScale3D(FVector(CoreRadius/50.f));
    // The burst's maximum diameter is twice that of the incoming orb, without splash damage.
    const float BurstRadius=bImpacted?OrbRadius*FMath::Lerp(1.f,2.f,FMath::SmoothStep(0.f,0.22f,ImpactAge)):OrbRadius*Pulse;
    Shell->SetRelativeScale3D(FVector(BurstRadius/50.f));
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
}
