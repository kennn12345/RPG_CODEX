#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGWeatherSubsystem.h"
#include "RPGCalendarSubsystem.h"
#include "RPGSkyCycle.h"
#include "RPGWeatherEffectsComponent.h"
#include "RPGRainSurfaceComponent.h"
#include "RPGWaterZoneComponent.h"
#include "RPGWeatherRainMath.h"
#include "RPGGroundFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "HAL/IConsoleManager.h"
#include "RPGWaitSubsystem.h"
#include "RPGControllerInputComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "HAL/FileManager.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetMaterialLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherMathTest,"RPG.Weather.CalendarAndForecast",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherMathTest::RunTest(const FString&)
{
    TestTrue(TEXT("Full at day 30"),FMath::IsNearlyEqual(URPGWeatherSubsystem::MoonIllumination(29*86400.0),1.f));
    TestTrue(TEXT("New at day 15"),URPGWeatherSubsystem::MoonIllumination(14*86400.0)<.001f);
    TestTrue(TEXT("Waning half"),FMath::IsNearlyEqual(URPGWeatherSubsystem::MoonIllumination(6.5*86400.0),.5f,1.e-5f));
    TestTrue(TEXT("Waxing half"),FMath::IsNearlyEqual(URPGWeatherSubsystem::MoonIllumination(21.5*86400.0),.5f,1.e-5f));
    TestTrue(TEXT("Year boundary lunar continuity"),FMath::Abs(URPGWeatherSubsystem::MoonIllumination(360*86400.0-1)-URPGWeatherSubsystem::MoonIllumination(360*86400.0))<.001f);
    bool Shapes[7]{};
    const auto Clear=URPGWeatherSubsystem::Preset(ERPGWeatherType::Clear),Storm=URPGWeatherSubsystem::Preset(ERPGWeatherType::Storm);
    TestTrue(TEXT("Heavy cloud deck precedes rain"),URPGWeatherSubsystem::Blend(Clear,Storm,.5f).Rain<.01f);
    TestTrue(TEXT("Storm has opaque cloud coverage"),Storm.Clouds>.95f && Storm.Shelf>.99f);
    auto AYaw=Clear,BYaw=Clear;AYaw.WindYawDegrees=170;BYaw.WindYawDegrees=-170;
    TestTrue(TEXT("Wind blends across shortest arc"),FMath::Abs(URPGWeatherSubsystem::Blend(AYaw,BYaw,.5f).WindYawDegrees)>179);
    const FVector NorthRain=RPGRain::Velocity(.8f,1500,90);
    TestTrue(TEXT("Rain follows shared wind bearing"),FMath::Abs(NorthRain.X)<.01f && NorthRain.Y>0 && NorthRain.Z==-1500);
    bool Saw[6]{};
    for(int32 I=0;I<400;++I)
    {
        const double Time=I*21600.0+3600;
        const auto A=URPGWeatherSubsystem::NaturalWeather(Time,7319),B=URPGWeatherSubsystem::NaturalWeather(Time,7319);
        Saw[int32(A.Type)]=true;Shapes[int32(A.CloudStyle)]=true;TestEqual(TEXT("Weather deterministic after time skips"),A.Rain,B.Rain);
        TestTrue(TEXT("Physical parameters bounded"),A.Clouds>=0&&A.Clouds<=1&&A.Fog>=0&&A.Fog<=1&&A.Rain>=0&&A.Rain<=1);
        const auto Left=URPGWeatherSubsystem::NaturalWeather(Time-3600-.1,7319),Right=URPGWeatherSubsystem::NaturalWeather(Time-3600+.1,7319);
        TestTrue(TEXT("Front boundary is continuous"),FMath::Abs(Left.Clouds-Right.Clouds)<.001f);
    }
    for(int32 I=2;I<7;++I)TestTrue(FString::Printf(TEXT("Forecast includes cloud shape %d"),I),Shapes[I]);
    for(int32 I=0;I<6;++I)TestTrue(FString::Printf(TEXT("Forecast includes type %d"),I),Saw[I]);
    return true;
}

namespace
{
class FWeatherRuntimeReview:public IAutomationLatentCommand
{
public:
    explicit FWeatherRuntimeReview(FAutomationTestBase* In):Test(In),Start(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        auto* LP=PC?PC->GetLocalPlayer():nullptr;
        auto* C=World&&World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>():nullptr;
        auto* W=World&&World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>():nullptr;
        auto* Menu=LP?LP->GetSubsystem<URPGWaitSubsystem>():nullptr;
        ARPGSkyCycle* Sky=nullptr;if(World)for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;break;}
        if(FPlatformTime::Seconds()-Start>60){Test->AddError(TEXT("Weather runtime timed out"));if(Menu)Menu->CancelWait();return true;}
        if(!C||!W||!Menu||!Sky||World->GetTimeSeconds()<2)return false;
        auto Key=[](FKey K){FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        switch(Step++)
        {
        case 0:
        {
            C->SetElapsedGameSeconds(29*86400.0);W->SetWeatherOverride(ERPGWeatherType::Clear,0);
            Test->TestTrue(TEXT("Sky knows full moon"),Sky->GetMoonIllumination()>.99f);
            for(TActorIterator<AActor> It(World);It;++It)if(auto* Volume=It->FindComponentByClass<UVolumetricCloudComponent>())
            {
                auto* M=Cast<UMaterialInstanceDynamic>(Volume->GetMaterial());Test->TestNotNull(TEXT("Clouds use weather material instance"),M);
                if(M){Test->TestNotNull(TEXT("Cloud height profiles survive material duplication"),M->K2_GetTextureParameterValue(TEXT("Layout_CloudHeightProfile")));Test->TestNotNull(TEXT("Cloud volume noise is bound"),M->K2_GetTextureParameterValue(TEXT("Noise_Texture3D")));}
            }
            auto* Controls=PC->GetPawn()?PC->GetPawn()->FindComponentByClass<URPGControllerInputComponent>():nullptr;
            auto* Action=Controls?FindObject<UInputAction>(Controls,TEXT("IA_WeatherTest")):nullptr;
            Test->TestNotNull(TEXT("Weather test input created"),Action);
            if(Action)LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->InjectInputForAction(Action,FInputActionValue(true),{},{});
            break;
        }
        case 1:
            if(!Menu->IsMenuOpen()){--Step;return false;}
            Test->TestFalse(TEXT("Weather panel leaves time running"),UGameplayStatics::IsGamePaused(World));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MoonWeather/TestPanel.png"),true,false);
            Hold=FPlatformTime::Seconds()+1;break;
        case 2:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            Key(EKeys::Escape);Test->TestFalse(TEXT("Weather panel closes with Escape"),Menu->IsMenuOpen());
            Test->TestFalse(TEXT("Weather panel restores movement"),PC->IsMoveInputIgnored());
            C->SetElapsedGameSeconds(14*86400.0);Test->TestTrue(TEXT("Sky follows new moon after date jump"),Sky->GetMoonIllumination()<.001f);
            W->SetWeatherOverride(ERPGWeatherType::Storm,8);C->AdvanceRealSeconds(4);
            Test->TestTrue(TEXT("Cloud deck develops before rain"),W->GetCurrentWeather().Clouds>.4f&&W->GetCurrentWeather().Rain<.1f);
            C->AdvanceGameHours(24);Test->TestTrue(TEXT("Waiting completes weather transition"),W->GetCurrentWeather().Rain>.99f);
            Test->TestEqual(TEXT("Rain bounded to one instanced batch"),Sky->WeatherEffects->GetRainInstanceCount(),12288);
            Sky->WeatherEffects->TriggerLightning();Hold=FPlatformTime::Seconds()+.15;break;
        case 3:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            Test->TestTrue(TEXT("Lightning flash active"),Sky->WeatherEffects->GetLightningFlash()>0);
            Hold=FPlatformTime::Seconds()+4.3;break;
        case 4:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            Test->TestTrue(TEXT("Rain audio plays"),Sky->WeatherEffects->IsRainAudioPlaying());
            Test->TestTrue(TEXT("Thunder follows lightning"),Sky->WeatherEffects->IsThunderAudioPlaying());
            W->bLightning=false;W->bRainEffects=false;W->bWeatherAudio=false;
            Hold=FPlatformTime::Seconds()+.2;break;
        case 5:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            Test->TestEqual(TEXT("Lightning toggle disables flash"),Sky->WeatherEffects->GetLightningFlash(),0.f);
            Test->TestFalse(TEXT("Audio toggle stops rain"),Sky->WeatherEffects->IsRainAudioPlaying());
            Test->TestFalse(TEXT("Audio toggle stops thunder"),Sky->WeatherEffects->IsThunderAudioPlaying());
            W->SetWeatherOverride(ERPGWeatherType::Clear,0);C->SetTimeOfDay(12);
            if(auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/RPG/Environment/Weather/MPC_WeatherLighting.MPC_WeatherLighting")))
            {
                Test->TestTrue(TEXT("Noon sunlight drives water caustics"),UKismetMaterialLibrary::GetScalarParameterValue(World,Collection,TEXT("DirectSunlight"))>.99f);
                C->SetTimeOfDay(0);
                Test->TestEqual(TEXT("No solar water glow at night"),UKismetMaterialLibrary::GetScalarParameterValue(World,Collection,TEXT("DirectSunlight")),0.f);
            }
            else Test->AddError(TEXT("Water lighting collection missing"));
            W->SetWeatherOverride(ERPGWeatherType::Fog,0);Sky->RefreshSky();Hold=FPlatformTime::Seconds()+3;break;
        case 6:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            if(Sky->Fog)
            {
                auto* Fog=Sky->Fog->GetComponent();
                Test->TestEqual(TEXT("Fog reaches the camera"),Fog->StartDistance,0.f);
                Test->TestEqual(TEXT("Volumetric fog reaches the camera"),Fog->VolumetricFogStartDistance,0.f);
                Test->TestTrue(TEXT("Global fog blanket removed for ground-only fog"),Fog->FogDensity<.001f);
                Test->TestTrue(TEXT("Terrain fog volumes allocated"),Sky->GroundFog->GetVolumeCount()>0);
                Test->TestEqual(TEXT("Fog cap is twenty feet"),Sky->GroundFog->LayerHeight,609.6f);
                TArray<ULocalFogVolumeComponent*> Volumes;Sky->GetComponents(Volumes);bool DenseVolume=false;
                for(auto* Volume:Volumes)DenseVolume|=Volume->IsVisible() && Volume->RadialFogExtinction>0 && Volume->HeightFogExtinction>0;
                Test->TestTrue(TEXT("Ground fog has both density inputs required by the renderer"),DenseVolume);
            }
            {
                TArray<ULocalFogVolumeComponent*> Volumes;Sky->GetComponents(Volumes);
                for(auto* Volume:Volumes)FogPositions.Add(Volume,Volume->GetComponentLocation());
                FVector Position;FRotator Rotation;PC->GetPlayerViewPoint(Position,Rotation);
                FogCamera=World->SpawnActor<ACameraActor>(Position+FVector(3000,0,0),Rotation);PC->SetViewTarget(FogCamera.Get());
            }
            Hold=FPlatformTime::Seconds()+1;break;
        case 7:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            {
                int32 Retained=0;
                for(const auto& Pair:FogPositions)if(Pair.Key.IsValid())
                {
                    ++Retained;Test->TestTrue(TEXT("Retained fog cells never relocate"),Pair.Key->GetComponentLocation().Equals(Pair.Value,.01));
                }
                Test->TestTrue(TEXT("Crossing grid boundaries retains overlapping fog cells"),Retained>FogPositions.Num()*.7);
                Test->TestTrue(TEXT("Fog pool remains bounded while running"),Sky->GroundFog->GetVolumeCount()<256);
                if(FogCamera.IsValid()){PC->SetViewTarget(PC->GetPawn());FogCamera->Destroy();}
            }
            WindOffset=Sky->GetCloudWindOffset();W->SetWindDirection(90,0);
            Test->TestTrue(TEXT("Changing wind cannot jump cloud coordinates"),Sky->GetCloudWindOffset().Equals(WindOffset));
            Test->TestTrue(TEXT("Wind test override reaches north"),FMath::Abs(W->GetCurrentWeather().WindYawDegrees-90)<.01);
            Hold=FPlatformTime::Seconds()+.5;break;
        case 8:
            if(FPlatformTime::Seconds()<Hold){--Step;return false;}
            {const FVector Travel=Sky->GetCloudWindOffset()-WindOffset;Test->TestTrue(TEXT("Clouds advect continuously along shared wind"),Travel.Y>0 && FMath::Abs(Travel.X)<1);}
            W->ResumeAutomaticWind(0);Test->TestFalse(TEXT("Automatic wind restored"),W->IsWindOverridden());
            W->ResumeAutomaticWeather(0);Test->TestFalse(TEXT("Automatic weather restored"),W->IsWeatherOverridden());
            return true;
        }
        return false;
    }
private:FAutomationTestBase* Test;double Start,Hold=0;int32 Step=0;
    TMap<TWeakObjectPtr<ULocalFogVolumeComponent>,FVector> FogPositions;TWeakObjectPtr<ACameraActor> FogCamera;FVector WindOffset;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherRuntimeTest,"RPG.Weather.RuntimeAndPanel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWeatherRuntimeReview(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FWeatherVisualReview:public IAutomationLatentCommand
{
public:
    FWeatherVisualReview():Start(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        if(!PC || World->GetTimeSeconds()<2)return FPlatformTime::Seconds()-Start>90;
        auto* C=World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>();
        auto* W=World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>();
        ARPGSkyCycle* Sky=nullptr;for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;break;}
        if(!Sky)return true;
        if(!Camera.IsValid())
        {
            Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=80;
            Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera.Get());
            for(TActorIterator<ACameraActor> It(World);It;++It)
            {
                if(It->GetActorLabel()==TEXT("WS_01_Path"))Ground=It->GetActorTransform();
                if(It->GetActorLabel()==TEXT("WS_06_LakeShore"))Water=It->GetActorTransform();
                if(It->GetActorLabel()==TEXT("WS_11_AerialEast"))Flight=It->GetActorTransform();
            }
            Sky->bAnimate=false;Dir=FPaths::ProjectSavedDir()/TEXT("MoonWeather/Review");IFileManager::Get().MakeDirectory(*Dir,true);
        }
        const TCHAR* Names[]={TEXT("01_full"),TEXT("02_waning_half"),TEXT("03_waning_crescent"),TEXT("04_new_night"),TEXT("05_waxing_crescent"),TEXT("06_waxing_half"),TEXT("07_clear"),TEXT("08_haze"),TEXT("09_cloudy"),TEXT("10_fog"),TEXT("11_rain"),TEXT("12_storm"),TEXT("13_water_noon"),TEXT("14_water_moonless"),TEXT("15_water_fullmoon"),TEXT("16_flight_fog"),TEXT("17_rain_fullmoon"),TEXT("18_clouds_up"),TEXT("19_puffs"),TEXT("20_billows"),TEXT("21_broken"),TEXT("22_overcast"),TEXT("23_storm_deck"),TEXT("24_clear_breaks")};
        if(Index>=UE_ARRAY_COUNT(Names)){Sky->bAnimate=true;Camera->Destroy();return true;}
        if(Stage==0)
        {
            static const int32 Days[]={30,8,12,15,19,23};static const float Hours[]={0,4,5.2f,0,19.2f,20};
            const auto Type=Index<6?ERPGWeatherType::Clear:Index<12?ERPGWeatherType(Index-6):Index==15?ERPGWeatherType::Fog:Index==16?ERPGWeatherType::Rain:Index==17?ERPGWeatherType::Cloudy:ERPGWeatherType::Clear;
            W->SetWeatherOverride(Type,0);
            if(Index>=18){const ERPGCloudStyle Styles[]={ERPGCloudStyle::Scattered,ERPGCloudStyle::Billowing,ERPGCloudStyle::Broken,ERPGCloudStyle::Overcast,ERPGCloudStyle::StormFront,ERPGCloudStyle::Clear};W->PreviewCloudStyle(Styles[Index-18],0);}
            W->bLightning=false;
            C->SetElapsedGameSeconds(Index<6?(Days[Index]-1)*86400.0+Hours[Index]*3600:29*86400.0+12*3600);
            if(Index==13)C->SetElapsedGameSeconds(14*86400.0);
            if(Index==14||Index==16)C->SetElapsedGameSeconds(29*86400.0);
            if(Index<6)
            {
                const FVector Location(2000,4200,20000);const FVector Direction=Index==3?FVector(.5,0,.866):Sky->GetMoonDirection();
                Camera->SetActorLocation(Location);Camera->SetActorRotation(Direction.Rotation());
            }
            else if(Index>=12 && Index<=14)Camera->SetActorTransform(Water);
            else if(Index==15)Camera->SetActorTransform(Flight);
            else if(Index>=17){Camera->SetActorLocation(Ground.GetLocation());Camera->SetActorRotation(FRotator(Index>=18?10.f:35.f,140,0));}
            else Camera->SetActorTransform(Ground);
            Sky->RefreshSky();
            for(TActorIterator<AActor> It(World);It;++It)if(auto* Volume=It->FindComponentByClass<UVolumetricCloudComponent>())
            {
                auto* M=Cast<UMaterialInstanceDynamic>(Volume->GetMaterial());
                UE_LOG(LogTemp,Display,TEXT("CloudReview: index=%d visible=%d hidden=%d material=%s coverage=%.3f density=%.3f bottom=%.2f height=%.2f"),Index,Volume->IsVisible(),It->IsHidden(),*GetPathNameSafe(M),M?M->K2_GetScalarParameterValue(TEXT("Cloud_GlobalCoverage")):-99,M?M->K2_GetScalarParameterValue(TEXT("Cloud_GlobalDensity")):-99,Volume->LayerBottomAltitude,Volume->LayerHeight);
            }
            Hold=FPlatformTime::Seconds()+3;Stage=1;return false;
        }
        if(FPlatformTime::Seconds()<Hold)return false;
        if(Stage==1){FScreenshotRequest::RequestScreenshot(Dir/FString(Names[Index])+TEXT(".png"),false,false);Hold=FPlatformTime::Seconds()+.5;Stage=2;return false;}
        ++Index;Stage=0;return false;
    }
private:double Start,Hold=0;int32 Index=0,Stage=0;FString Dir;TWeakObjectPtr<ACameraActor> Camera;FTransform Ground,Water,Flight;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherVisualTest,"Visual.MoonWeather",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FWeatherVisualTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWeatherVisualReview());ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FRainMotionReview:public IAutomationLatentCommand
{
public:
    FRainMotionReview():Started(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        if(!PC || World->GetTimeSeconds()<2)return FPlatformTime::Seconds()-Started>90;
        if(!Camera.IsValid())
        {
            for(TActorIterator<ACameraActor> It(World);It;++It)if(It->GetActorLabel()==TEXT("WS_01_Path")){Base=It->GetActorTransform();break;}
            Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=80;
            Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera.Get());
            for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;Sky->bAnimate=false;break;}
            Dir=FPaths::ProjectSavedDir()/TEXT("RainRevision/Motion");IFileManager::Get().MakeDirectory(*Dir,true);
        }
        if(Stage>=7){if(Sky.IsValid())Sky->bAnimate=true;Camera->Destroy();return true;}
        if(!Configured)
        {
            auto* C=World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>();
            auto* W=World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>();
            W->SetWeatherOverride(Stage==6?ERPGWeatherType::Fog:ERPGWeatherType::Rain,0);W->bLightning=false;
            C->SetElapsedGameSeconds(Stage==0||Stage>=4?29*86400.0+12*3600:Stage==1?29*86400.0+17.3*3600:Stage==2?29*86400.0:14*86400.0);
            Sky->RefreshSky();Configured=true;StageStart=FPlatformTime::Seconds();NextCapture=StageStart+2;
        }
        const double Now=FPlatformTime::Seconds(),T=FMath::Max(0.0,Now-StageStart-2);
        const FVector Offset=Stage==4?FVector(T*15000,0,4000):Stage==5?FVector(-T*15000,0,4000+T*5000):Stage==6?FVector(T*700,0,0):FVector(T*150,0,0);
        Camera->SetActorLocation(Base.GetLocation()+Offset);
        Camera->SetActorRotation(Base.Rotator()+FRotator(0,Stage==6?0:T*8,0));
        if(Now<NextCapture)return false;
        FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("%d_%03d.png"),Stage,Frame),false,false);
        NextCapture=Now+.1;
        if(++Frame>=40){Frame=0;++Stage;Configured=false;}
        return false;
    }
private:
    double Started,StageStart=0,NextCapture=0;int32 Stage=0,Frame=0;bool Configured=false;
    FTransform Base;FString Dir;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<ARPGSkyCycle> Sky;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRainMotionTest,"Visual.RainMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRainMotionTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FRainMotionReview());ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRainFieldTest,"RPG.Weather.FastFlightRainField",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRainFieldTest::RunTest(const FString&)
{
    const FVector Fall=RPGRain::Velocity(.7f,1400);
    for(double Radius:{1800.0,5500.0})
        for(FVector CameraStep:{FVector(15000,0,0),FVector(-15000,10000,8000),FVector(1.e7,-1.e7,-1.e7)})
        {
            const FVector Local(300,200,150);
            const FVector Wrapped=RPGRain::WrapLocal(Local+Fall/30-CameraStep,Radius);
            TestTrue(TEXT("Immediate coverage after fast movement/teleport"),FMath::Abs(Wrapped.X)<=Radius && FMath::Abs(Wrapped.Y)<=Radius && Wrapped.Z>=-600 && Wrapped.Z<1600);
            if(CameraStep.Z==0)TestTrue(TEXT("Horizontal flight preserves rain height, not a top-only respawn"),FMath::IsNearlyEqual(Wrapped.Z,Local.Z+Fall.Z/30,.001));
            const FVector UnwrappedDelta=CameraStep+Wrapped-Local-Fall/30;
            TestTrue(TEXT("Flight only recycles boundaries; it does not change fall velocity"),FMath::IsNearlyZero(FMath::Fmod(FMath::Abs(UnwrappedDelta.X)+.001,Radius*2),.01));
        }
    return true;
}
namespace
{
class FSilhouetteAB:public IAutomationLatentCommand
{
public:
    bool Update()override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        if(!PC||World->GetTimeSeconds()<2)return false;
        if(!Camera.IsValid())
        {
            FVector P;FRotator R;PC->GetPlayerViewPoint(P,R);P.Z+=1800;R.Pitch=0;
            Camera=World->SpawnActor<ACameraActor>();Camera->SetActorLocation(P);Camera->SetActorRotation(R);Camera->GetCameraComponent()->FieldOfView=85;
            Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera.Get());
            for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;Sky->bAnimate=false;break;}
            for(TActorIterator<AActor> It(World);It;++It)if(auto* C=It->FindComponentByClass<UVolumetricCloudComponent>()){Cloud=C;break;}
            Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.Mode"));OriginalMode=Mode->GetInt();
            AA=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));OriginalAA=AA->GetInt();
            Target=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget"));OriginalTarget=Target->GetInt();
            Dir=FPaths::ProjectSavedDir()/TEXT("CloudFogRevision/AB");IFileManager::Get().MakeDirectory(*Dir,true);
        }
        if(Index>=8)
        {
            Mode->Set(OriginalMode,ECVF_SetByCode);AA->Set(OriginalAA,ECVF_SetByCode);Target->Set(OriginalTarget,ECVF_SetByCode);Cloud->SetVisibility(true);Sky->Fog->GetComponent()->SetVisibility(true);
            Sky->bAnimate=true;Camera->Destroy();return true;
        }
        if(!Configured)
        {
            auto* C=World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>();auto* W=World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>();
            W->SetWeatherOverride(ERPGWeatherType::Rain,0);W->bLightning=false;W->bRainEffects=Index!=1;
            C->SetElapsedGameSeconds(29*86400.0+17.3*3600);Sky->RefreshSky();
            Cloud->SetVisibility(Index!=2);Sky->Fog->GetComponent()->SetVisibility(Index!=3);if(Index==3)Sky->GroundFog->SetWeather(0);
            Mode->Set(Index==4?1:Index==5?3:0,ECVF_SetByCode);AA->Set(Index==6?0:OriginalAA,ECVF_SetByCode);
            if(Index==7)IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget"))->Set(0,ECVF_SetByCode);
            Configured=true;Hold=FPlatformTime::Seconds()+3;return false;
        }
        if(FPlatformTime::Seconds()<Hold)return false;
        const TCHAR* Names[]={TEXT("0_all"),TEXT("1_no_rain"),TEXT("2_no_clouds"),TEXT("3_no_fog"),TEXT("4_cloud_mode1"),TEXT("5_cloud_mode3"),TEXT("6_no_aa"),TEXT("7_cloud_direct")};
        FScreenshotRequest::RequestScreenshot(Dir/FString(Names[Index])+TEXT(".png"),false,false);++Index;Configured=false;return false;
    }
private:
    TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<ARPGSkyCycle> Sky;TWeakObjectPtr<UVolumetricCloudComponent> Cloud;
    IConsoleVariable* Mode=nullptr;IConsoleVariable* AA=nullptr;IConsoleVariable* Target=nullptr;int32 OriginalMode=0,OriginalAA=0,OriginalTarget=1,Index=0;bool Configured=false;double Hold=0;FString Dir;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilhouetteTest,"Visual.WeatherSilhouetteAB",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSilhouetteTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));ADD_LATENT_AUTOMATION_COMMAND(FSilhouetteAB());ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FRainSurfaceReview:public IAutomationLatentCommand
{
public:
    explicit FRainSurfaceReview(FAutomationTestBase* In):Test(In),Start(FPlatformTime::Seconds()){}
    bool Update()override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        if(!PC||World->GetTimeSeconds()<2)return FPlatformTime::Seconds()-Start>90;
        auto* C=World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>();auto* W=World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>();
        if(!Camera.IsValid())
        {
            for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;Sky->bAnimate=false;break;}
            float Area=0;
            for(TActorIterator<AActor> It(World);It;++It){TArray<URPGWaterZoneComponent*> Zones;It->GetComponents(Zones);for(auto* Z:Zones){const auto E=Z->GetScaledBoxExtent();if(E.X*E.Y>Area){Area=E.X*E.Y;Water=Z;}}}
            if(!Sky.IsValid()||!Water.IsValid()){Test->AddError(TEXT("Rain surface test requires sky and water"));return true;}
            Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=75;Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera.Get());
            const FVector Center=Water->GetComponentLocation();Camera->SetActorLocation(FVector(Center.X-300,Center.Y,Water->GetSurfaceHeight(Center)+180));Camera->SetActorRotation(FRotator(-25,0,0));
            Dir=FPaths::ProjectSavedDir()/TEXT("RainRealism/Review");IFileManager::Get().MakeDirectory(*Dir,true);
        }
        if(Index>=7){Sky->bAnimate=true;PC->SetViewTarget(PC->GetPawn());Camera->Destroy();return true;}
        if(!Configured)
        {
            W->bRainEffects=true;W->bLightning=false;W->SetWeatherOverride(Index==0||Index==3||Index>=5?ERPGWeatherType::Clear:ERPGWeatherType::Storm,0);
            C->SetElapsedGameSeconds((Index==4?14:29)*86400.0+(Index==4?0:12*3600));Sky->RainSurfaces->PreviewWetness(Index==6?0:Index>=2?1:0);Sky->RefreshSky();
            if(Index==5)for(TActorIterator<ACameraActor> It(World);It;++It)if(It->GetActorLabel()==TEXT("WS_01_Path")){Camera->SetActorLocation(It->GetActorLocation()+FVector(0,0,25));auto R=It->GetActorRotation();R.Pitch=-20;Camera->SetActorRotation(R);break;}
            Hold=FPlatformTime::Seconds()+3;Configured=true;return false;
        }
        if(FPlatformTime::Seconds()<Hold)return false;
        Test->TestTrue(TEXT("Impact trace budget bounded"),Sky->RainSurfaces->GetTraceCountLastTick()<=12);
        Test->TestTrue(TEXT("Impact pool bounded"),Sky->RainSurfaces->GetActiveImpactCount()<=256);
        if(Index==1){Test->TestTrue(TEXT("Rain produces surface impacts"),Sky->RainSurfaces->GetActiveImpactCount()>0);Test->TestTrue(TEXT("Rain produces actual water ripples"),Sky->RainSurfaces->GetWaterImpactCount()>0);Test->TestTrue(TEXT("Wetness accumulates"),Sky->RainSurfaces->GetWetness()>.01f);}
        if(Index==3)Test->TestTrue(TEXT("Wetness remains after rain ends"),Sky->RainSurfaces->GetWetness()>.9f);
        const TCHAR* Names[]={TEXT("01_dry_water"),TEXT("02_rain_impacts"),TEXT("03_soaked_storm"),TEXT("04_drying_clear"),TEXT("05_moonless_rain"),TEXT("06_wet_ground"),TEXT("07_dry_ground")};
        FScreenshotRequest::RequestScreenshot(Dir/FString(Names[Index])+TEXT(".png"),false,false);++Index;Configured=false;return false;
    }
private:FAutomationTestBase* Test;double Start,Hold=0;int32 Index=0;bool Configured=false;FString Dir;TWeakObjectPtr<ACameraActor> Camera;TWeakObjectPtr<ARPGSkyCycle> Sky;TWeakObjectPtr<URPGWaterZoneComponent> Water;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRainSurfaceTest,"RPG.Weather.SurfaceRain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRainSurfaceTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FRainSurfaceReview(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
