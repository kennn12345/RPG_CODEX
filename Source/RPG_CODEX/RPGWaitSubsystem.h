#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RPGWaitSubsystem.generated.h"

class APlayerController;
class UGameViewportClient;
class SWidget;

/** Local modal waiting window. Choosing hours never changes time until confirmed. */
UCLASS()
class RPG_CODEX_API URPGWaitSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category="Wait") bool OpenWaitMenu(APlayerController* Controller, bool bSleep = false);
    UFUNCTION(BlueprintCallable, Category="Weather") bool OpenWeatherTestMenu(APlayerController* Controller);
    UFUNCTION(BlueprintCallable, Category="Wait") void SetSelectedHours(int32 Hours);
    UFUNCTION(BlueprintCallable, Category="Wait") bool ConfirmWait();
    UFUNCTION(BlueprintCallable, Category="Wait") void CancelWait();
    UFUNCTION(BlueprintPure, Category="Wait") bool IsMenuOpen() const { return bMenuOpen; }
    bool IsWeatherTestMenuOpen() const { return bMenuOpen && bWeatherMenu; }
    UFUNCTION(BlueprintPure, Category="Wait") int32 GetSelectedHours() const { return SelectedHours; }
    UFUNCTION(BlueprintPure, Category="Wait") bool IsSleepMenu() const { return bSleepMenu; }
    FText GetCurrentDateText() const;
    FText GetCurrentTimeText() const;
    FText GetCurrentWeekText() const;
    FText GetArrivalText() const;
    TSharedPtr<SWidget> GetMenuWidget() const { return MenuWidget; }

private:
    void CloseMenu();
    void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    TSharedPtr<SWidget> MenuWidget;
    TWeakObjectPtr<APlayerController> ActiveController;
    TWeakObjectPtr<UGameViewportClient> ActiveViewport;
    TWeakObjectPtr<UWorld> MenuWorld;
    bool bMenuOpen = false;
    bool bConfirming = false;
    bool bSleepMenu = false;
    bool bWeatherMenu = false;
    bool bWasPaused = false;
    bool bWasMouseVisible = false;
    bool bWasHUDVisible = true;
    int32 SelectedHours = 1;
};
