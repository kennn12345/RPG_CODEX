#include "RPGWeatherTestPanel.h"
#include "RPGWaitSubsystem.h"
#include "RPGWeatherSubsystem.h"
#include "RPGCalendarSubsystem.h"
#include "RPGSkyCycle.h"
#include "RPGWeatherEffectsComponent.h"
#include "RPGRainSurfaceComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class SWeatherPanel:public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWeatherPanel){} SLATE_ARGUMENT(TWeakObjectPtr<URPGWaitSubsystem>,Owner) SLATE_END_ARGS()
    UGameInstance* Instance() const{return Owner.IsValid() && Owner->GetLocalPlayer()?Owner->GetLocalPlayer()->GetGameInstance():nullptr;}
    URPGWeatherSubsystem* Weather() const{return Instance()?Instance()->GetSubsystem<URPGWeatherSubsystem>():nullptr;}
    URPGCalendarSubsystem* Calendar() const{return Instance()?Instance()->GetSubsystem<URPGCalendarSubsystem>():nullptr;}
    TSharedRef<SWidget> Button(const TCHAR* Label,TFunction<void()> Action)
    {
        return SNew(SButton).TextStyle(&ButtonTextStyle).ContentPadding(FMargin(9,6)).Text(FText::FromString(Label)).OnClicked_Lambda([Action]{Action();return FReply::Handled();});
    }
    TSharedRef<SWidget> Heading(const TCHAR* Label){return SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold",18));}
    TSharedRef<SWidget> WeatherButton(const TCHAR* Label,ERPGWeatherType Type){return Button(Label,[this,Type]{if(Weather())Weather()->SetWeatherOverride(Type,bSmooth?8.f:0.f);});}
    TSharedRef<SWidget> CloudButton(const TCHAR* Label,ERPGCloudStyle Style){return Button(Label,[this,Style]{if(Weather())Weather()->PreviewCloudStyle(Style,bSmooth?8.f:0.f);});}
    void MoonDay(int32 Day)
    {
        auto* C=Calendar();if(!C)return;
        const int64 Month=C->GetDateTime().ElapsedDays/30;
        C->SetElapsedGameSeconds((Month*30+Day-1)*86400.0);
        // Place the moon near its highest point so crescent/quarter previews are easy to inspect.
        const float Hour=Day==8?4.f:Day==12?5.2f:Day==15?12.f:Day==19?19.2f:Day==23?20.f:0.f;
        C->SetTimeOfDay(Hour);
    }
    void Construct(const FArguments& Args)
    {
        Owner=Args._Owner; ButtonTextStyle=FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"); ButtonTextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",14));
        ChildSlot.HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(16)
        [ SNew(SBox).WidthOverride(530).MaxDesiredHeight(950)
            [SNew(SBorder).Padding(18).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.04f,.055f,.96f))
                [SNew(SScrollBox)+SScrollBox::Slot()
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Heading(TEXT("Sky & weather testing"))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
                        [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).AutoWrapText(true).Text_Lambda([this]{
                            auto* C=Calendar();auto* W=Weather();if(!C||!W)return FText::GetEmpty();
                            return FText::FromString(C->FormatDate(C->GetDateTime()).ToString()+TEXT("  ")+C->FormatTime(C->GetDateTime()).ToString()+TEXT("\n")
                                +(W->IsWeatherOverridden()?TEXT("Override: "):TEXT("Automatic: "))+W->WeatherName(W->GetCurrentWeather().Type)
                                +FString::Printf(TEXT("  |  Moon %.0f%% lit\n%s | Wind toward %.0f deg (0=E,90=N), %.1f m/s"),100*W->MoonIllumination(C->GetElapsedGameSeconds()),*W->CloudName(W->GetCurrentWeather().CloudStyle),W->GetCurrentWeather().WindYawDegrees,W->GetCurrentWeather().WindVelocity().Size()/100.f));
                        })]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Heading(TEXT("Weather"))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Clear"),ERPGWeatherType::Clear)]
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Haze"),ERPGWeatherType::Haze)]
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Cloudy"),ERPGWeatherType::Cloudy)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Fog"),ERPGWeatherType::Fog)]
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Rain"),ERPGWeatherType::Rain)]
                            +SHorizontalBox::Slot().FillWidth(1)[WeatherButton(TEXT("Storm"),ERPGWeatherType::Storm)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,8)
                        [Button(TEXT("Resume automatic weather"),[this]{if(Weather())Weather()->ResumeAutomaticWeather(bSmooth?8.f:0.f);})]
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Cloud shapes"))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[CloudButton(TEXT("Puffs"),ERPGCloudStyle::Scattered)]
                            +SHorizontalBox::Slot().FillWidth(1)[CloudButton(TEXT("Tall billows"),ERPGCloudStyle::Billowing)]
                            +SHorizontalBox::Slot().FillWidth(1)[CloudButton(TEXT("Broken"),ERPGCloudStyle::Broken)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[CloudButton(TEXT("Overcast"),ERPGCloudStyle::Overcast)]
                            +SHorizontalBox::Slot().FillWidth(1)[CloudButton(TEXT("Storm deck"),ERPGCloudStyle::StormFront)]]
                        +SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Wind direction (toward)"))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("NE"),[this]{if(Weather())Weather()->SetWindDirection(45);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("North"),[this]{if(Weather())Weather()->SetWindDirection(90);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("East"),[this]{if(Weather())Weather()->SetWindDirection(0);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("SW"),[this]{if(Weather())Weather()->SetWindDirection(225);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Auto"),[this]{if(Weather())Weather()->ResumeAutomaticWind();})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                        [SNew(SCheckBox).IsChecked_Lambda([this]{return bSmooth?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){bSmooth=S==ECheckBoxState::Checked;})
                            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text(FText::FromString(TEXT("Blend test changes over 8 seconds")))]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Heading(TEXT("Moon phase (changes calendar day)"))]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Full: day 30"),[this]{MoonDay(30);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Waning half: day 8"),[this]{MoonDay(8);})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("New: day 15"),[this]{MoonDay(15);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Waxing half: day 23"),[this]{MoonDay(23);})]]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Waning crescent"),[this]{MoonDay(12);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Waxing crescent"),[this]{MoonDay(19);})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Previous day"),[this]{if(Calendar())Calendar()->SetElapsedGameSeconds(FMath::Max(0.0,Calendar()->GetElapsedGameSeconds()-86400));})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Next day"),[this]{if(Calendar())Calendar()->AdvanceGameHours(24);})]]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Midnight"),[this]{if(Calendar())Calendar()->SetTimeOfDay(0);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Noon"),[this]{if(Calendar())Calendar()->SetTimeOfDay(12);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Lightning"),[this]{if(Instance())for(TActorIterator<ARPGSkyCycle> It(Instance()->GetWorld());It;++It)if(It->WeatherEffects)It->WeatherEffects->TriggerLightning();})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Preview dry ground"),[this]{if(Instance())for(TActorIterator<ARPGSkyCycle> It(Instance()->GetWorld());It;++It)if(It->RainSurfaces)It->RainSurfaces->PreviewWetness(0);})]
                            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("Preview wet ground"),[this]{if(Instance())for(TActorIterator<ARPGSkyCycle> It(Instance()->GetWorld());It;++It)if(It->RainSurfaces)It->RainSurfaces->PreviewWetness(1);})]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,4)[SNew(SCheckBox)
                            .IsChecked_Lambda([this]{return Weather()&&Weather()->bRainEffects?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Weather())Weather()->bRainEffects=S==ECheckBoxState::Checked;})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text(FText::FromString(TEXT("Visible rain")))]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,2)[SNew(SCheckBox)
                            .IsChecked_Lambda([this]{return Weather()&&Weather()->bLightning?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Weather())Weather()->bLightning=S==ECheckBoxState::Checked;})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text(FText::FromString(TEXT("Lightning flashes")))]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,12)[SNew(SCheckBox)
                            .IsChecked_Lambda([this]{return Weather()&&Weather()->bWeatherAudio?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Weather())Weather()->bWeatherAudio=S==ECheckBoxState::Checked;})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text(FText::FromString(TEXT("Rain & thunder audio")))]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).AutoWrapText(true).Text(FText::FromString(TEXT("Moon buttons set the date and a useful viewing hour. The moon changes position: look around/up. Weather continues while this panel is open. Tab / D-Pad navigates; Enter / A selects.")))]
                        +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Close (F6 / Esc / B)"),[this]{if(Owner.IsValid())Owner->CancelWait();})]
                    ]
                ]
            ]
        ];
    }
    bool SupportsKeyboardFocus() const override{return true;}
    FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
    {
        if(Event.GetKey()==EKeys::Escape||Event.GetKey()==EKeys::F6||Event.GetKey()==EKeys::Gamepad_FaceButton_Right)
        {if(Owner.IsValid())Owner->CancelWait();return FReply::Handled();}
        return FReply::Unhandled();
    }
private:TWeakObjectPtr<URPGWaitSubsystem> Owner;bool bSmooth=false;FTextBlockStyle ButtonTextStyle;
};
}
TSharedRef<SWidget> MakeRPGWeatherTestPanel(URPGWaitSubsystem* Owner){return SNew(SWeatherPanel).Owner(Owner);}
