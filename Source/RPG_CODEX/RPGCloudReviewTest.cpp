#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGWeatherSubsystem.h"
#include "RPGCalendarSubsystem.h"
#include "RPGSkyCycle.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

namespace
{
/** Noon sky photos of every cloud style, framed for comparison with real cloud photography. */
class FCloudStyleNoonReview:public IAutomationLatentCommand
{
public:
    FCloudStyleNoonReview():Start(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?UGameplayStatics::GetPlayerController(World,0):nullptr;
        if(!PC || World->GetTimeSeconds()<2)return FPlatformTime::Seconds()-Start>90;
        auto* C=World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>();
        auto* W=World->GetGameInstance()->GetSubsystem<URPGWeatherSubsystem>();
        ARPGSkyCycle* Sky=nullptr;for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;break;}
        if(!Sky||!C||!W)return true;
        if(!Camera.IsValid())
        {
            Camera=World->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=90;
            Camera->GetCameraComponent()->bConstrainAspectRatio=false;PC->SetViewTarget(Camera.Get());
            for(TActorIterator<ACameraActor> It(World);It;++It)if(It->GetActorLabel()==TEXT("WS_04_Overlook"))Spot=It->GetActorTransform();
            Sky->bAnimate=false;W->bLightning=false;
            // Look-dev options: -CloudTag=Name -CloudStyles=25 -CloudScalar="A=1;B=2" -CloudVector="C=1,1,1,1"
            FString Tag=TEXT("Noon"),Value;FParse::Value(FCommandLine::Get(),TEXT("CloudTag="),Tag);
            if(FParse::Value(FCommandLine::Get(),TEXT("CloudStyles="),Value))for(TCHAR Ch:Value)if(Ch>='1'&&Ch<='6')Only.Add(Ch-'1');
            TArray<FString> Items;
            if(FParse::Value(FCommandLine::Get(),TEXT("CloudScalar="),Value,false))
            {
                Value.TrimQuotesInline();Value.ParseIntoArray(Items,TEXT(";"));
                for(const FString& I:Items){FString K,V;if(I.Split(TEXT("="),&K,&V))Sky->CloudScalarOverrides.Add(FName(*K.TrimStartAndEnd()),FCString::Atof(*V));}
            }
            if(FParse::Value(FCommandLine::Get(),TEXT("CloudVector="),Value,false))
            {
                Value.TrimQuotesInline();Value.ParseIntoArray(Items,TEXT(";"));
                for(const FString& I:Items)
                {
                    FString K,V;TArray<FString> N;if(!I.Split(TEXT("="),&K,&V))continue;V.ParseIntoArray(N,TEXT(","));
                    FLinearColor Col(0,0,0,0);for(int32 j=0;j<FMath::Min(4,N.Num());++j)Col.Component(j)=FCString::Atof(*N[j]);
                    Sky->CloudVectorOverrides.Add(FName(*K.TrimStartAndEnd()),Col);
                }
            }
            if(FParse::Value(FCommandLine::Get(),TEXT("CloudTexture="),Value,false))
            {
                Value.TrimQuotesInline();Value.ParseIntoArray(Items,TEXT(";"));
                for(const FString& I:Items){FString K,V;if(I.Split(TEXT("="),&K,&V))Sky->CloudTextureOverrides.Add(FName(*K.TrimStartAndEnd()),V.TrimStartAndEnd());}
            }
            for(const auto& O:Sky->CloudTextureOverrides)UE_LOG(LogTemp,Display,TEXT("CloudLook override %s=%s"),*O.Key.ToString(),*O.Value);
            for(const auto& O:Sky->CloudScalarOverrides)UE_LOG(LogTemp,Display,TEXT("CloudLook override %s=%.4f"),*O.Key.ToString(),O.Value);
            for(const auto& O:Sky->CloudVectorOverrides)UE_LOG(LogTemp,Display,TEXT("CloudLook override %s=%s"),*O.Key.ToString(),*O.Value.ToString());
            Dir=FPaths::ProjectSavedDir()/TEXT("CloudLook")/Tag;IFileManager::Get().MakeDirectory(*Dir,true);
        }
        while(Only.Num() && Index<12 && !Only.Contains(Index/2))++Index;
        static const ERPGCloudStyle Styles[]={ERPGCloudStyle::Clear,ERPGCloudStyle::Scattered,ERPGCloudStyle::Billowing,ERPGCloudStyle::Broken,ERPGCloudStyle::Overcast,ERPGCloudStyle::StormFront};
        static const TCHAR* Names[]={TEXT("1_clear"),TEXT("2_puffs"),TEXT("3_billows"),TEXT("4_broken"),TEXT("5_overcast"),TEXT("6_storm_deck")};
        static const TCHAR* Views[]={TEXT("horizon"),TEXT("up")};
        static const float Pitches[]={6.f,38.f};
        if(Index>=UE_ARRAY_COUNT(Styles)*2){Sky->bAnimate=true;Sky->CloudScalarOverrides.Empty();Sky->CloudVectorOverrides.Empty();Sky->CloudTextureOverrides.Empty();Camera->Destroy();return true;}
        const int32 Style=Index/2,View=Index%2;
        if(Stage==0)
        {
            if(View==0)W->PreviewCloudStyle(Styles[Style],0);
            C->SetElapsedGameSeconds(29*86400.0+12*3600);
            Camera->SetActorLocation(Spot.GetLocation());
            Camera->SetActorRotation(FRotator(Pitches[View],Spot.Rotator().Yaw,0));
            Sky->RefreshSky();
            // Let the temporal cloud reconstruction settle on a new style.
            Hold=FPlatformTime::Seconds()+(View==0?5:2);Stage=1;return false;
        }
        if(FPlatformTime::Seconds()<Hold)return false;
        if(Stage==1)
        {
            // Captures the PIE game view (HighResShot grabs the editor viewport instead).
            FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("%s_%s.png"),Names[Style],Views[View]),false,false);
            Hold=FPlatformTime::Seconds()+1.5;Stage=2;return false;
        }
        ++Index;Stage=0;return false;
    }
private:double Start,Hold=0;int32 Index=0,Stage=0;FString Dir;TWeakObjectPtr<ACameraActor> Camera;FTransform Spot;TArray<int32> Only;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCloudStyleNoonTest,"Visual.CloudStylesNoon",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCloudStyleNoonTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCloudStyleNoonReview());ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
