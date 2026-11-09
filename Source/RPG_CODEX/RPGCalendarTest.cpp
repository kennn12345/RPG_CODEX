#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RPGCalendarSubsystem.h"
#include "RPGWaitSubsystem.h"
#include "RPGSkyCycle.h"
#include "RPGControllerInputComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCalendarArithmetic,"RPG.Calendar.Arithmetic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGCalendarArithmetic::RunTest(const FString&)
{
    UGameInstance* Instance=NewObject<UGameInstance>();
    URPGCalendarSubsystem* Calendar=NewObject<URPGCalendarSubsystem>(Instance);
    TestEqual(TEXT("Full cycle is 120 real minutes"),Calendar->RealMinutesPerDay,120.f);
    Calendar->SetElapsedGameSeconds(0);
    Calendar->AdvanceRealSeconds(3600);
    TestEqual(TEXT("One real hour is half a game day"),Calendar->GetDateTime().Hour,12);
    Calendar->AdvanceRealSeconds(3600);
    TestEqual(TEXT("Two real hours is next day"),Calendar->GetDateTime().Day,2);
    Calendar->SetElapsedGameSeconds(29*86400.0+23*3600+59*60);
    const double Before=Calendar->GetElapsedGameSeconds();
    const auto Preview=Calendar->GetDateTimeAfterHours(1);
    TestEqual(TEXT("Preview crosses month"),Preview.Month,2);
    TestEqual(TEXT("Preview does not change clock"),Calendar->GetElapsedGameSeconds(),Before);
    Calendar->AdvanceGameHours(1);
    TestEqual(TEXT("Month rollover day"),Calendar->GetDateTime().Day,1);
    TestEqual(TEXT("Wait preserves minutes"),Calendar->GetDateTime().Minute,59);
    Calendar->SetElapsedGameSeconds(359*86400.0+23*3600);
    Calendar->AdvanceGameHours(1);
    const auto NewYear=Calendar->GetDateTime();
    TestEqual(TEXT("Year rollover"),NewYear.Year,2);
    TestEqual(TEXT("Year rollover month"),NewYear.Month,1);
    TestEqual(TEXT("Weekdays continue over year boundary"),NewYear.Weekday,4);
    TestEqual(TEXT("Week of year restarts"),NewYear.WeekOfYear,1);
    TestEqual(TEXT("Seven days is next week"),URPGCalendarSubsystem::FromElapsedSeconds(7*86400.0).WeekOfYear,2);
    TestEqual(TEXT("Seven days cycles weekday"),URPGCalendarSubsystem::FromElapsedSeconds(7*86400.0).Weekday,1);
    TestFalse(TEXT("Negative skip rejected"),Calendar->AdvanceGameHours(-1));
    TestFalse(TEXT("Overflow rejected"),Calendar->AdvanceGameHours(1.e20));
    TestEqual(TEXT("Rejected skips preserve state"),Calendar->GetDateTime().Year,2);
    Calendar->SetTimeOfDay(6.5f);
    TestEqual(TEXT("Clock-only edit preserves year"),Calendar->GetDateTime().Year,2);
    TestEqual(TEXT("Clock-only edit minute"),Calendar->GetDateTime().Minute,30);
    return true;
}

namespace
{
class FWaitPopupReview : public IAutomationLatentCommand
{
public:
    explicit FWaitPopupReview(FAutomationTestBase* InTest):Test(InTest),Started(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        ACharacter* Player=World?UGameplayStatics::GetPlayerCharacter(World,0):nullptr;
        APlayerController* PC=Player?Cast<APlayerController>(Player->GetController()):nullptr;
        ULocalPlayer* LP=PC?PC->GetLocalPlayer():nullptr;
        auto* Wait=LP?LP->GetSubsystem<URPGWaitSubsystem>():nullptr;
        auto* Calendar=World && World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>():nullptr;
        auto* Input=LP?LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        auto* Controls=Player?Player->FindComponentByClass<URPGControllerInputComponent>():nullptr;
        auto* Action=Controls?FindObject<UInputAction>(Controls,TEXT("IA_WaitSleep")):nullptr;
        if(FPlatformTime::Seconds()-Started>60){Test->AddError(TEXT("Wait popup timed out"));if(Wait)Wait->CancelWait();return true;}
        if(!Wait || !Calendar || !Input || !Action || World->GetTimeSeconds()<2)return false;
        ARPGSkyCycle* Sky=nullptr;
        for(TActorIterator<ARPGSkyCycle> It(World);It;++It){Sky=*It;break;}
        auto Key=[](FKey Value){FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Value,FModifierKeysState(),0,false,0,0));};
        switch(Step++)
        {
        case 0:
            if(Sky)Sky->bAnimate=false;
            Calendar->SetElapsedGameSeconds(29*86400.0+23*3600+30*60);
            Before=Calendar->GetElapsedGameSeconds();
            Input->InjectInputForAction(Action,FInputActionValue(true),{},{});
            break;
        case 1:
            if(!Wait->IsMenuOpen()){--Step;return false;}
            Test->TestTrue(TEXT("D-Pad Down action opens popup"),Wait->IsMenuOpen());
            Test->TestTrue(TEXT("Popup pauses game"),UGameplayStatics::IsGamePaused(World));
            Test->TestTrue(TEXT("Movement blocked while choosing"),PC->IsMoveInputIgnored());
            Wait->SetSelectedHours(0);Test->TestEqual(TEXT("Minimum is one hour"),Wait->GetSelectedHours(),1);
            Wait->SetSelectedHours(99);Test->TestEqual(TEXT("Maximum is 24 hours"),Wait->GetSelectedHours(),24);
            Wait->SetSelectedHours(1);Key(EKeys::Gamepad_DPad_Right);
            Test->TestEqual(TEXT("D-Pad Right changes selection"),Wait->GetSelectedHours(),2);
            Test->TestEqual(TEXT("Choosing does not advance clock"),Calendar->GetElapsedGameSeconds(),Before);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("CalendarWait/WaitPopup.png"),true,false);
            HoldUntil=FPlatformTime::Seconds()+1;
            break;
        case 2:
            if(FPlatformTime::Seconds()<HoldUntil){--Step;return false;}
            Key(EKeys::Gamepad_FaceButton_Right);
            Test->TestFalse(TEXT("B closes popup"),Wait->IsMenuOpen());
            Test->TestFalse(TEXT("Cancel unpauses world"),UGameplayStatics::IsGamePaused(World));
            Test->TestFalse(TEXT("Move input restored"),PC->IsMoveInputIgnored());
            Test->TestEqual(TEXT("Cancel preserves time"),Calendar->GetElapsedGameSeconds(),Before);
            break;
        case 3:
            Test->TestTrue(TEXT("Popup can reopen"),Wait->OpenWaitMenu(PC));
            Wait->SetSelectedHours(2);Key(EKeys::Gamepad_FaceButton_Bottom);
            Test->TestFalse(TEXT("A closes popup"),Wait->IsMenuOpen());
            Test->TestEqual(TEXT("A advances exactly selected hours"),Calendar->GetElapsedGameSeconds(),Before+7200);
            Test->TestEqual(TEXT("Waiting crosses month"),Calendar->GetDateTime().Month,2);
            Test->TestEqual(TEXT("Waiting crosses midnight"),Calendar->GetDateTime().Hour,1);
            if(Sky)Test->TestTrue(TEXT("Sky follows waiting clock"),FMath::IsNearlyEqual(Sky->TimeOfDay,1.5f));
            Test->TestFalse(TEXT("Duplicate confirm does nothing"),Wait->ConfirmWait());
            UGameplayStatics::SetGamePaused(World,true);
            Test->TestTrue(TEXT("Sleep menu opens while already paused"),Wait->OpenWaitMenu(PC,true));
            Test->TestTrue(TEXT("Sleep label retained"),Wait->IsSleepMenu());
            Wait->CancelWait();
            Test->TestTrue(TEXT("Existing pause retained"),UGameplayStatics::IsGamePaused(World));
            UGameplayStatics::SetGamePaused(World,false);
            if(Sky)Sky->bAnimate=true;
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;double Started,Before=0,HoldUntil=0;int32 Step=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWaitPopupTest,"RPG.Calendar.WaitPopup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FRPGWaitPopupTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitPopupReview(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
