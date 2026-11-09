#include "RPGWaitSubsystem.h"

#include "RPGCalendarSubsystem.h"
#include "RPGWeatherTestPanel.h"
#include "RPGCombatStateComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
URPGCalendarSubsystem* CalendarFor(const URPGWaitSubsystem* Wait)
{
    ULocalPlayer* Player = Wait ? Wait->GetLocalPlayer() : nullptr;
    UGameInstance* Instance = Player ? Player->GetGameInstance() : nullptr;
    return Instance ? Instance->GetSubsystem<URPGCalendarSubsystem>() : nullptr;
}

class SRPGWaitWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRPGWaitWindow) {} SLATE_ARGUMENT(TWeakObjectPtr<URPGWaitSubsystem>, Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Owner = Args._Owner;
        const FLinearColor Muted(0.63f,0.7f,0.77f);
        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,0.60f)) ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(520.f)
                [
                    SNew(SBorder).Padding(1.f).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.25f,0.35f,0.43f))
                    [
                        SNew(SBorder).Padding(28.f).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.025f,0.038f,0.052f,1.f))
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
                            [ SNew(STextBlock).Text_Lambda([this]{ return FText::FromString(Owner.IsValid() && Owner->IsSleepMenu() ? TEXT("Sleep") : TEXT("Wait")); }).Font(FCoreStyle::GetDefaultFontStyle("Bold",28)).ColorAndOpacity(FLinearColor::White) ]
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(FText::FromString(TEXT("CURRENT TIME"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(Muted) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                            [ SNew(STextBlock).Text_Lambda([this]{ return Owner.IsValid() ? Owner->GetCurrentDateText() : FText::GetEmpty(); }).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).ColorAndOpacity(FLinearColor::White) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                            [ SNew(STextBlock).Text_Lambda([this]{ return Owner.IsValid() ? Owner->GetCurrentTimeText() : FText::GetEmpty(); }).Font(FCoreStyle::GetDefaultFontStyle("Bold",32)).ColorAndOpacity(FLinearColor::White) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)
                            [ SNew(STextBlock).Text_Lambda([this]{ return Owner.IsValid() ? Owner->GetCurrentWeekText() : FText::GetEmpty(); }).Font(FCoreStyle::GetDefaultFontStyle("Regular",13)).ColorAndOpacity(Muted) ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(SButton).IsFocusable(false).Text(FText::FromString(TEXT("-"))).ContentPadding(FMargin(15,5)).OnClicked_Lambda([this]{ Adjust(-1); return FReply::Handled(); }) ]
                                + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [ SNew(STextBlock).Text_Lambda([this]{ const int32 Hours=Owner.IsValid()?Owner->GetSelectedHours():1; return FText::FromString(FString::Printf(TEXT("%d %s"),Hours,Hours==1?TEXT("hour"):TEXT("hours"))); }).Font(FCoreStyle::GetDefaultFontStyle("Bold",24)).ColorAndOpacity(FLinearColor::White) ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(SButton).IsFocusable(false).Text(FText::FromString(TEXT("+"))).ContentPadding(FMargin(15,5)).OnClicked_Lambda([this]{ Adjust(1); return FReply::Handled(); }) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,14,0,20)
                            [ SNew(SSlider).IsFocusable(false).MouseUsesStep(true).StepSize(1.f/23.f)
                                .Value_Lambda([this]{ return Owner.IsValid()?(Owner->GetSelectedHours()-1)/23.f:0.f; })
                                .OnValueChanged_Lambda([this](float Value){ if(Owner.IsValid())Owner->SetSelectedHours(1+FMath::RoundToInt(Value*23)); }) ]
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(STextBlock).Text(FText::FromString(TEXT("AFTER WAITING"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(Muted) ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,22)
                            [ SNew(STextBlock).Text_Lambda([this]{ return Owner.IsValid()?Owner->GetArrivalText():FText::GetEmpty(); }).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).ColorAndOpacity(FLinearColor(0.74f,0.86f,0.95f)).AutoWrapText(true) ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,7,0)
                                [ SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(12,9)).ButtonColorAndOpacity(FLinearColor(0.23f,0.36f,0.46f))
                                    .Text_Lambda([this]{return FText::FromString(Owner.IsValid()&&Owner->IsSleepMenu()?TEXT("Sleep"):TEXT("Wait"));})
                                    .OnClicked_Lambda([this]{if(Owner.IsValid())Owner->ConfirmWait();return FReply::Handled();}) ]
                                + SHorizontalBox::Slot().FillWidth(1).Padding(7,0,0,0)
                                [ SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(12,9)).Text(FText::FromString(TEXT("Cancel")))
                                    .OnClicked_Lambda([this]{if(Owner.IsValid())Owner->CancelWait();return FReply::Handled();}) ]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)
                            [ SNew(STextBlock).Text(FText::FromString(TEXT("D-Pad / arrows: hours   A / Enter: wait   B / Esc: cancel"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(Muted).AutoWrapText(true) ]
                        ]
                    ]
                ]
            ]
        ];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        const FKey Key=Event.GetKey();
        if(Key==EKeys::Left || Key==EKeys::Gamepad_DPad_Left){Adjust(-1);return FReply::Handled();}
        if(Key==EKeys::Right || Key==EKeys::Gamepad_DPad_Right){Adjust(1);return FReply::Handled();}
        if(Key==EKeys::Enter || Key==EKeys::SpaceBar || Key==EKeys::Gamepad_FaceButton_Bottom)
        {if(Owner.IsValid())Owner->ConfirmWait();return FReply::Handled();}
        if(Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right || Key==EKeys::Gamepad_DPad_Down)
        {if(Owner.IsValid())Owner->CancelWait();return FReply::Handled();}
        return FReply::Handled();
    }
private:
    void Adjust(int32 Amount){if(Owner.IsValid())Owner->SetSelectedHours(Owner->GetSelectedHours()+Amount);}
    TWeakObjectPtr<URPGWaitSubsystem> Owner;
};
}

void URPGWaitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FWorldDelegates::OnWorldCleanup.AddUObject(this,&URPGWaitSubsystem::OnWorldCleanup);
}
void URPGWaitSubsystem::Deinitialize()
{
    CloseMenu();
    FWorldDelegates::OnWorldCleanup.RemoveAll(this);
    Super::Deinitialize();
}
void URPGWaitSubsystem::OnWorldCleanup(UWorld* World,bool,bool)
{
    if(MenuWorld.Get()==World)CloseMenu();
}

bool URPGWaitSubsystem::OpenWaitMenu(APlayerController* Controller,bool bSleep)
{
    if(bMenuOpen || !Controller || !Controller->IsLocalController() || Controller->GetLocalPlayer()!=GetLocalPlayer())return false;
    UWorld* World=Controller->GetWorld();
    UGameViewportClient* Viewport=GetLocalPlayer()->ViewportClient;
    if(!World || !World->IsGameWorld() || !Viewport || !CalendarFor(this) || !FSlateApplication::IsInitialized())return false;
    if(const APawn* Pawn=Controller->GetPawn())
    {
        const URPGCombatStateComponent* State=Pawn->FindComponentByClass<URPGCombatStateComponent>();
        if(State && (State->bIsInAir || State->bIsFlying || State->bIsBlocking || State->bIsChargingAttack || State->bIsChargingKick))return false;
        if(const ACharacter* Character=Cast<ACharacter>(Pawn);Character && Character->GetCharacterMovement()->IsFalling())return false;
    }
    bWasPaused=UGameplayStatics::IsGamePaused(World);
    if(!bWeatherMenu && !bWasPaused && !UGameplayStatics::SetGamePaused(World,true))return false;
    ActiveController=Controller;ActiveViewport=Viewport;MenuWorld=World;
    bWasMouseVisible=Controller->bShowMouseCursor;
    if(auto* HUD=Controller->GetHUD()){bWasHUDVisible=HUD->bShowHUD;if(bWeatherMenu)HUD->bShowHUD=false;}
    bSleepMenu=bSleep;SelectedHours=1;bMenuOpen=true;bConfirming=false;
    Controller->SetIgnoreMoveInput(true);Controller->SetIgnoreLookInput(true);Controller->FlushPressedKeys();
    if(ACharacter* Character=Cast<ACharacter>(Controller->GetPawn()))Character->GetCharacterMovement()->StopMovementImmediately();
    MenuWidget=bWeatherMenu?MakeRPGWeatherTestPanel(this):StaticCastSharedRef<SWidget>(SNew(SRPGWaitWindow).Owner(this));
    if(bWeatherMenu)Viewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(),1000);
    else Viewport->AddViewportWidgetForPlayer(GetLocalPlayer(),MenuWidget.ToSharedRef(),200);
    Controller->bShowMouseCursor=true;
    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(MenuWidget);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Controller->SetInputMode(Mode);
    FSlateApplication::Get().SetUserFocus(FMath::Max(0,GetLocalPlayer()->GetControllerId()),MenuWidget,EFocusCause::SetDirectly);
    return true;
}

bool URPGWaitSubsystem::OpenWeatherTestMenu(APlayerController* Controller)
{
    if(bMenuOpen){if(bWeatherMenu)CloseMenu();return false;}
    bWeatherMenu=true;
    if(OpenWaitMenu(Controller))return true;
    bWeatherMenu=false;return false;
}

void URPGWaitSubsystem::SetSelectedHours(int32 Hours){SelectedHours=FMath::Clamp(Hours,1,24);}
bool URPGWaitSubsystem::ConfirmWait()
{
    if(!bMenuOpen || bConfirming)return false;
    bConfirming=true;
    URPGCalendarSubsystem* Calendar=CalendarFor(this);
    const bool Result=Calendar && Calendar->AdvanceGameHours(SelectedHours);
    CloseMenu();
    return Result;
}
void URPGWaitSubsystem::CancelWait(){CloseMenu();}
void URPGWaitSubsystem::CloseMenu()
{
    if(!bMenuOpen)return;
    bMenuOpen=false;
    if(ActiveViewport.IsValid() && MenuWidget.IsValid())
    {
        if(bWeatherMenu)ActiveViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
        else ActiveViewport->RemoveViewportWidgetForPlayer(GetLocalPlayer(),MenuWidget.ToSharedRef());
    }
    if(ActiveController.IsValid())
    {
        APlayerController* Controller=ActiveController.Get();
        Controller->SetIgnoreMoveInput(false);Controller->SetIgnoreLookInput(false);
        Controller->FlushPressedKeys();
        Controller->bShowMouseCursor=bWasMouseVisible;
        if(bWeatherMenu)if(auto* HUD=Controller->GetHUD())HUD->bShowHUD=bWasHUDVisible;
        Controller->SetInputMode(FInputModeGameOnly());
        if(!bWasPaused && MenuWorld.IsValid())UGameplayStatics::SetGamePaused(MenuWorld.Get(),false);
    }
    MenuWidget.Reset();ActiveController.Reset();ActiveViewport.Reset();MenuWorld.Reset();bConfirming=false;bWeatherMenu=false;
}

FText URPGWaitSubsystem::GetCurrentDateText() const
{
    const auto* Calendar=CalendarFor(this);return Calendar?Calendar->FormatDate(Calendar->GetDateTime()):FText::GetEmpty();
}
FText URPGWaitSubsystem::GetCurrentTimeText() const
{
    const auto* Calendar=CalendarFor(this);return Calendar?Calendar->FormatTime(Calendar->GetDateTime()):FText::GetEmpty();
}
FText URPGWaitSubsystem::GetCurrentWeekText() const
{
    const auto* Calendar=CalendarFor(this);return Calendar?Calendar->FormatWeek(Calendar->GetDateTime()):FText::GetEmpty();
}
FText URPGWaitSubsystem::GetArrivalText() const
{
    const auto* Calendar=CalendarFor(this);
    if(!Calendar)return FText::GetEmpty();
    const auto Arrival=Calendar->GetDateTimeAfterHours(SelectedHours);
    return FText::FromString(Calendar->FormatDate(Arrival).ToString()+TEXT("\n")+Calendar->FormatTime(Arrival).ToString()+TEXT("  |  ")+Calendar->FormatWeek(Arrival).ToString());
}
