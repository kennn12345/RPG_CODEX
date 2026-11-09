#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RPGCalendarSubsystem.generated.h"

class ARPGSkyCycle;

/** Fantasy calendar: 24 hours, seven weekdays, twelve 30-day months. All fields are one-based except the clock. */
USTRUCT(BlueprintType)
struct RPG_CODEX_API FRPGCalendarDateTime
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Year = 1;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Month = 1;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Day = 1;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Weekday = 1;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 WeekOfYear = 1;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Hour = 0;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Minute = 0;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int32 Second = 0;
    UPROPERTY(BlueprintReadOnly, Category="Calendar") int64 ElapsedDays = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGCalendarChanged, const FRPGCalendarDateTime&, DateTime);

/** One clock per game instance. Survives level changes; the sky is its visual receiver. */
UCLASS(Config=Game)
class RPG_CODEX_API URPGCalendarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;

    /** Complete sunrise-to-sunrise cycle. 120 = about 60 minutes daylight and 60 minutes night. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Calendar", meta=(ClampMin="0.1"))
    float RealMinutesPerDay = 120.f;

    /** Names can be supplied later; empty entries display numbered placeholders. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Calendar") TArray<FString> WeekdayNames;
    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Calendar") TArray<FString> MonthNames;
    UPROPERTY(BlueprintAssignable, Category="Calendar") FRPGCalendarChanged OnCalendarChanged;

    UFUNCTION(BlueprintPure, Category="Calendar") FRPGCalendarDateTime GetDateTime() const;
    UFUNCTION(BlueprintPure, Category="Calendar") FRPGCalendarDateTime GetDateTimeAfterHours(double Hours) const;
    UFUNCTION(BlueprintPure, Category="Calendar") float GetTimeOfDay() const;
    UFUNCTION(BlueprintPure, Category="Calendar") double GetElapsedGameSeconds() const { return ElapsedGameSeconds; }
    UFUNCTION(BlueprintPure, Category="Calendar") FText FormatDate(const FRPGCalendarDateTime& Value) const;
    UFUNCTION(BlueprintPure, Category="Calendar") FText FormatTime(const FRPGCalendarDateTime& Value) const;
    UFUNCTION(BlueprintPure, Category="Calendar") FText FormatWeek(const FRPGCalendarDateTime& Value) const;

    /** Advance without wrapping the date away. Negative/non-finite/out-of-range jumps are rejected atomically. */
    UFUNCTION(BlueprintCallable, Category="Calendar") bool AdvanceGameHours(double Hours);
    /** State hooks for a future game save. New play sessions start a new calendar; no hidden autosave. */
    UFUNCTION(BlueprintCallable, Category="Calendar") bool SetElapsedGameSeconds(double Seconds);
    /** Change the clock within the current date, preserving the date (also used by sky previews/tests). */
    UFUNCTION(BlueprintCallable, Category="Calendar") void SetTimeOfDay(float Hours);

    void AdvanceRealSeconds(double Seconds);
    void RegisterSky(ARPGSkyCycle* Sky);
    static FRPGCalendarDateTime FromElapsedSeconds(double Seconds);

private:
    void SyncSky();
    void NotifyIfMinuteChanged();
    double ElapsedGameSeconds = 0.0;
    bool bInitialized = false;
    int64 LastNotifiedMinute = -1;
    TWeakObjectPtr<ARPGSkyCycle> PrimarySky;
};
