#include "RPGRainSurfaceComponent.h"
#include "RPGWeatherSubsystem.h"
#include "RPGWaterZoneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInterface.h"

URPGRainSurfaceComponent::URPGRainSurfaceComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;Random.Initialize(71893);
}
void URPGRainSurfaceComponent::BeginPlay()
{
    Super::BeginPlay();if(GetNetMode()==NM_DedicatedServer){SetComponentTickEnabled(false);return;}
    Lighting=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/RPG/Environment/Weather/MPC_WeatherLighting.MPC_WeatherLighting"));
    auto Make=[this](const TCHAR* Name,const TCHAR* Material)
    {
        auto* Mesh=NewObject<UInstancedStaticMeshComponent>(GetOwner(),Name);GetOwner()->AddInstanceComponent(Mesh);
        Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);Mesh->bReceivesDecals=false;Mesh->NumCustomDataFloats=2;
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
        Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));Mesh->RegisterComponent();return Mesh;
    };
    Rings=Make(TEXT("RainWaterRipples"),TEXT("/Game/RPG/Environment/Weather/M_Rain_Ripple.M_Rain_Ripple"));
    Splashes=Make(TEXT("RainSurfaceSplashes"),TEXT("/Game/RPG/Environment/Weather/M_Rain_Splash.M_Rain_Splash"));
    Impacts.SetNum(PoolSize);RingTransforms.Init(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),PoolSize);SplashTransforms=RingTransforms;
    Rings->AddInstances(RingTransforms,false,false,false);Splashes->AddInstances(SplashTransforms,false,false,false);
    for(TActorIterator<AActor> It(GetWorld());It;++It){TArray<URPGWaterZoneComponent*> Zones;It->GetComponents(Zones);for(auto* Zone:Zones)WaterZones.Add(Zone);}
}
int32 URPGRainSurfaceComponent::GetActiveImpactCount()const{int32 N=0;for(const auto& I:Impacts)N+=I.Age<I.Life;return N;}
int32 URPGRainSurfaceComponent::GetWaterImpactCount()const{int32 N=0;for(const auto& I:Impacts)N+=I.Age<I.Life&&I.Water.IsValid();return N;}
void URPGRainSurfaceComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);LastTraces=0;if(!Rings||!GetWorld())return;
    auto* PC=GetWorld()->GetFirstPlayerController();auto* GI=GetWorld()->GetGameInstance();auto* W=GI?GI->GetSubsystem<URPGWeatherSubsystem>():nullptr;
    if(!PC||!PC->IsLocalController()||!W)return;
    const auto Weather=W->GetCurrentWeather();
    // Surface wetness builds over a minute and dries over several minutes, independent of the visual-effect toggle.
    Wetness=FMath::Clamp(Wetness+Delta*(Weather.Rain>.01f?Weather.Rain/60.f:-1.f/360.f),0.f,1.f);
    if(Lighting){UKismetMaterialLibrary::SetScalarParameterValue(this,Lighting,TEXT("RainIntensity"),Weather.Rain);UKismetMaterialLibrary::SetScalarParameterValue(this,Lighting,TEXT("Wetness"),Wetness);}
    FVector Camera;FRotator View;PC->GetPlayerViewPoint(Camera,View);
    SpawnCredit=FMath::Min(SpawnCredit+Delta*(W->bRainEffects?Weather.Rain*300.f:0),12.f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RainSurfaceImpact),false,GetOwner());if(PC->GetPawn())Query.AddIgnoredActor(PC->GetPawn());
    while(SpawnCredit>=1)
    {
        --SpawnCredit;++LastTraces;
        const float Angle=Random.FRand()*2*PI,Radius=FMath::Sqrt(Random.FRand())*1600;
        const FVector XY=Camera+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);FHitResult Hit;
        if(!GetWorld()->LineTraceSingleByChannel(Hit,XY+FVector(0,0,2600),XY-FVector(0,0,6000),ECC_Visibility,Query))continue;
        FVector Position=Hit.ImpactPoint,Normal=Hit.ImpactNormal;URPGWaterZoneComponent* Water=nullptr;
        for(const auto& Weak:WaterZones)if(auto* Zone=Weak.Get())if(Zone->ContainsXY(Position))
        {
            const float Height=Zone->GetSurfaceHeight(Position);
            if(Height>=Position.Z && Height<Camera.Z+2600){Position.Z=Height;Normal=FVector::UpVector;Water=Zone;}
        }
        if(Normal.Z<.5f || FVector::DistSquared(Position,Camera)>FMath::Square(2500.f))continue;
        auto& I=Impacts[NextSlot];I.Position=Position;I.Normal=Normal;I.Water=Water;I.Age=0;
        I.Life=Water?Random.FRandRange(.45f,.7f):Random.FRandRange(.18f,.3f);I.Size=Random.FRandRange(.65f,1.25f);NextSlot=(NextSlot+1)%PoolSize;
    }
    for(int32 N=0;N<PoolSize;++N)
    {
        auto& I=Impacts[N];I.Age+=Delta;const bool Active=I.Age<I.Life;
        FVector Position=I.Position;if(auto* Water=I.Water.Get())Position.Z=Water->GetSurfaceHeight(Position);
        const float Distance=FVector::Distance(Position,Camera),Fade=1-FMath::SmoothStep(1800.f,2500.f,Distance);
        const float Age=I.Age/I.Life;
        Rings->SetCustomDataValue(N,0,Age,false);Rings->SetCustomDataValue(N,1,Fade,false);
        Splashes->SetCustomDataValue(N,0,I.Age/.26f,false);Splashes->SetCustomDataValue(N,1,Fade,false);
        const FVector ToCamera=(Camera-Position).GetSafeNormal();
        const FVector Side=FVector::CrossProduct(ToCamera,FVector::UpVector).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        RingTransforms[N]=FTransform(FQuat::FindBetweenNormals(FVector::UpVector,I.Normal),Position+I.Normal*1.2f,Active&&I.Water.IsValid()?FVector(I.Size*.28f):FVector::ZeroVector);
        SplashTransforms[N]=FTransform(FRotationMatrix::MakeFromXY(Side,FVector::UpVector).ToQuat(),Position+FVector(0,0,4*I.Size),Active&&I.Age<.26f?FVector(.1f*I.Size,.1f*I.Size,1):FVector::ZeroVector);
    }
    Rings->BatchUpdateInstancesTransforms(0,RingTransforms,true,true,true);Splashes->BatchUpdateInstancesTransforms(0,SplashTransforms,true,true,true);
}
