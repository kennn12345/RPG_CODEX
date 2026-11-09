#include "RPGWeatherEffectsComponent.h"
#include "RPGWeatherSubsystem.h"
#include "RPGWeatherRainMath.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr int32 RainCount=12288,SampleRate=22050;
TArray<uint8> MakeWeatherAudio(bool bThunder)
{
    const int32 Samples=SampleRate*(bThunder?6:4);TArray<uint8> Bytes;Bytes.SetNumUninitialized(Samples*2);
    FRandomStream Noise(bThunder?917:3271);float Low=0,Mid=0,Slow=0;
    for(int32 I=0;I<Samples;++I)
    {
        const float N=Noise.FRandRange(-1.f,1.f),T=float(I)/SampleRate;
        Low+=.012f*(N-Low);Mid+=.14f*(N-Mid);Slow+=.0015f*(N-Slow);
        float Value;
        if(bThunder)
        {
            const float Envelope=(1-FMath::Exp(-T*18))*FMath::Exp(-T*.8f)*FMath::Clamp((6-T)*2,0.f,1.f);
            Value=(Low*5+Mid*.45f+Slow*5+FMath::Sin(T*2*PI*47)*.08f)*Envelope*.7f;
        }
        else
        {
            // Low rumble and fine patter; cyclic fade avoids a loop seam.
            const float Edge=FMath::Min(1.f,FMath::Min(T,4-T)*40);
            Value=(N*.12f+Mid*.55f+Low*.9f)*Edge;
        }
        const int16 PCM=int16(FMath::Clamp(Value,-.95f,.95f)*32767);
        FMemory::Memcpy(Bytes.GetData()+I*2,&PCM,2);
    }
    return Bytes;
}
USoundWaveProcedural* MakeWave(UObject* Outer)
{
    auto* Wave=NewObject<USoundWaveProcedural>(Outer);Wave->SetSampleRate(SampleRate);Wave->NumChannels=1;
    Wave->Duration=INDEFINITELY_LOOPING_DURATION;return Wave;
}
}
URPGWeatherEffectsComponent::URPGWeatherEffectsComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    RainMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/RPG/Environment/Weather/M_Weather_Rain.M_Weather_Rain")));
    Random.Initialize(43291);
}
void URPGWeatherEffectsComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer){SetComponentTickEnabled(false);return;}
    AActor* Owner=GetOwner();
    RainMesh=NewObject<UInstancedStaticMeshComponent>(Owner,TEXT("WeatherRain"));Owner->AddInstanceComponent(RainMesh);
    RainMesh->SetMobility(EComponentMobility::Movable);RainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RainMesh->SetCanEverAffectNavigation(false);RainMesh->SetCastShadow(false);RainMesh->bReceivesDecals=false;
    RainMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
    if(auto* Material=RainMaterial.LoadSynchronous())RainMesh->SetMaterial(0,Material);
    RainMesh->NumCustomDataFloats=1;
    RainMesh->RegisterComponent();RainTransforms.Init(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),RainCount);
    RainPositions.SetNumZeroed(RainCount);
    RainMesh->AddInstances(RainTransforms,false,false,false);RainMesh->SetVisibility(false);
    for(int32 I=0;I<RainCount;++I){FRandomStream Drop(I*7919+431);RainMesh->SetCustomDataValue(I,0,Drop.FRandRange(.2f,1.f),false);}
    FlashLight=NewObject<UDirectionalLightComponent>(Owner,TEXT("WeatherLightning"));Owner->AddInstanceComponent(FlashLight);
    FlashLight->SetMobility(EComponentMobility::Movable);FlashLight->CastShadows=false;FlashLight->SetIntensity(0);
    FlashLight->SetLightColor(FLinearColor(.72f,.82f,1));FlashLight->SetWorldRotation(FRotator(-65,45,0));FlashLight->RegisterComponent();
    RainAudio=NewObject<UAudioComponent>(Owner);Owner->AddInstanceComponent(RainAudio);RainAudio->bAutoActivate=false;RainAudio->bAllowSpatialization=false;RainAudio->RegisterComponent();
    ThunderAudio=NewObject<UAudioComponent>(Owner);Owner->AddInstanceComponent(ThunderAudio);ThunderAudio->bAutoActivate=false;ThunderAudio->bAllowSpatialization=false;ThunderAudio->RegisterComponent();
    RainWave=MakeWave(this);ThunderWave=MakeWave(this);RainPCM=MakeWeatherAudio(false);ThunderPCM=MakeWeatherAudio(true);
    RainAudio->SetSound(RainWave);ThunderAudio->SetSound(ThunderWave);
    TraceTime=1;
}
void URPGWeatherEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(RainAudio)RainAudio->Stop();if(ThunderAudio)ThunderAudio->Stop();
    Super::EndPlay(Reason);
}
int32 URPGWeatherEffectsComponent::GetRainInstanceCount() const{return RainMesh?RainMesh->GetInstanceCount():0;}
bool URPGWeatherEffectsComponent::IsRainAudioPlaying() const{return RainAudio && RainAudio->IsPlaying();}
bool URPGWeatherEffectsComponent::IsThunderAudioPlaying() const{return ThunderAudio && ThunderAudio->IsPlaying();}
void URPGWeatherEffectsComponent::TriggerLightning()
{
    auto* GI=GetWorld()?GetWorld()->GetGameInstance():nullptr;
    auto* W=GI?GI->GetSubsystem<URPGWeatherSubsystem>():nullptr;
    if(!W || !W->bLightning)return;
    FlashAge=0;ThunderDelay=Random.FRandRange(1.8f,3.8f);NextFlash=Random.FRandRange(12.f,30.f);
}
void URPGWeatherEffectsComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    if(!RainMesh || !GetWorld() || UGameplayStatics::IsGamePaused(this))return;
    auto* GI=GetWorld()->GetGameInstance();auto* Weather=GI?GI->GetSubsystem<URPGWeatherSubsystem>():nullptr;
    APlayerController* PC=GetWorld()->GetFirstPlayerController();if(!PC || !PC->IsLocalController() || !Weather)return;
    const auto State=Weather->GetCurrentWeather();
    FVector Camera;FRotator Rotation;PC->GetPlayerViewPoint(Camera,Rotation);
    RainTime+=Delta;TraceTime+=Delta;FlashAge+=Delta;ThunderAge+=Delta;
    if(State.Storm>.55f && Weather->bLightning){NextFlash-=Delta;if(NextFlash<=0)TriggerLightning();}
    else NextFlash=FMath::Max(NextFlash,3.f);
    Flash=Weather->bLightning?(FMath::Exp(-FlashAge*24)+.55f*FMath::Exp(-FMath::Square((FlashAge-.18f)*28))):0;
    FlashLight->SetIntensity(Flash*12);
    if(ThunderDelay>=0)
    {
        ThunderDelay-=Delta;
        if(ThunderDelay<0 && Weather->bWeatherAudio && Weather->bLightning)
        {
            ThunderAudio->Stop();ThunderWave->ResetAudio();ThunderWave->QueueAudio(ThunderPCM.GetData(),ThunderPCM.Num());
            ThunderAudio->SetVolumeMultiplier(bSheltered?.3f:.6f);ThunderAudio->Play();ThunderAge=0;
        }
    }
    if(ThunderAge>6 || !Weather->bWeatherAudio)ThunderAudio->Stop();
    const float WantVolume=Weather->bWeatherAudio?State.Rain*(bSheltered?.17f:.65f):0;
    AudioVolume=Weather->bWeatherAudio?FMath::FInterpTo(AudioVolume,WantVolume,Delta,2.f):0;RainAudio->SetVolumeMultiplier(AudioVolume);
    if(AudioVolume>.005f)
    {
        if(RainWave->GetAvailableAudioByteCount()<SampleRate*2)RainWave->QueueAudio(RainPCM.GetData(),RainPCM.Num());
        if(!RainAudio->IsPlaying())RainAudio->Play();
    }
    else if(RainAudio->IsPlaying()){RainAudio->Stop();RainWave->ResetAudio();}
    const bool bRain=Weather->bRainEffects && State.Rain>.01f;RainMesh->SetVisibility(bRain);
    if(State.Rain>.01f && (TraceTime>.25f || FVector::DistSquared(Camera,PreviousRainCamera)>FMath::Square(350.f)))
    {
        TraceTime=0;FCollisionQueryParams Query(SCENE_QUERY_STAT(WeatherShelter),false,GetOwner());if(PC->GetPawn())Query.AddIgnoredActor(PC->GetPawn());
        for(int32 Field=0;Field<3;++Field)for(int32 Y=0;Y<5;++Y)for(int32 X=0;X<5;++X)
        {
            const float Spacing=Field==0?600.f:Field==1?1500.f:3000.f;
            const FVector Top=Camera+FVector((X-2)*Spacing,(Y-2)*Spacing,2600);FHitResult Hit;
            const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Top,Top-FVector(0,0,6000),ECC_Visibility,Query);
            FloorHeights[Field*25+Y*5+X]=bHit?float(Hit.ImpactPoint.Z)+10:float(Camera.Z)-4000;
        }
        bSheltered=FloorHeights[12]>Camera.Z+75;
    }
    if(!bRain){bRainWasVisible=false;return;}
    // Keep drops in world space: moving the camera must not drag the whole storm along.
    const bool ResetAll=!bRainInitialized || !bRainWasVisible;
    PreviousRainCamera=Camera;bRainInitialized=true;bRainWasVisible=true;
    RainMesh->SetWorldLocation(Camera);
    for(int32 I=0;I<RainCount;++I)
    {
        FRandomStream Drop(I*7919+431);
        const float Speed=Drop.FRandRange(550,980);
        const bool bSelected=Drop.FRand()<State.Rain;
        const FVector Velocity=RPGRain::Velocity(State.Wind,Speed,State.WindYawDegrees);
        const int32 Field=I<RainCount*.55f?0:I<RainCount*.9f?1:2;
        const float Radius=Field==0?900.f:Field==1?3000.f:6000.f;
        FVector& WorldPosition=RainPositions[I];
        if(ResetAll)WorldPosition=Camera+FVector(Random.FRandRange(-Radius,Radius),Random.FRandRange(-Radius,Radius),Random.FRandRange(RPGRain::MinimumHeight,RPGRain::MaximumHeight));
        else WorldPosition+=Velocity*Delta;
        // Recycle all three axes at the volume boundary, preserving height when flying
        // forward. New leading columns are populated immediately instead of falling
        // from the top and trying to catch a fast-moving player.
        const FVector Position=RPGRain::WrapLocal(WorldPosition-Camera,Radius);
        WorldPosition=Camera+Position;
        const float Spacing=Field==0?600.f:Field==1?1500.f:3000.f;
        const int32 IX=FMath::Clamp(FMath::RoundToInt(float(Position.X)/Spacing)+2,0,4),IY=FMath::Clamp(FMath::RoundToInt(float(Position.Y)/Spacing)+2,0,4);
        const bool bVisible=bSelected && WorldPosition.Z>FloorHeights[Field*25+IY*5+IX];
        const FVector Fall=Velocity.GetSafeNormal();
        const FVector Side=FVector::CrossProduct(Position.GetSafeNormal(),Fall).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        const FQuat Facing=FRotationMatrix::MakeFromXY(Side,Fall).ToQuat();
        const float NearFade=FMath::SmoothStep(120.f,300.f,float(Position.Size()));
        const float EdgeFade=1-FMath::SmoothStep(Radius*.75f,Radius,float(FMath::Max(FMath::Abs(Position.X),FMath::Abs(Position.Y))));
        // Thin anti-aliased geometry; streak length follows drop speed and a short exposure.
        const float Width=FMath::Clamp(float(Position.Size())*.0015f,.15f,4.f)/100.f*Drop.FRandRange(.65f,1.1f);
        RainTransforms[I]=FTransform(Facing,Position,bVisible?FVector(Width*NearFade*EdgeFade,(Speed/100.f)*Drop.FRandRange(.006f,.012f)*NearFade*EdgeFade,1):FVector::ZeroVector);
    }
    RainMesh->BatchUpdateInstancesTransforms(0,RainTransforms,false,true,true);
}
