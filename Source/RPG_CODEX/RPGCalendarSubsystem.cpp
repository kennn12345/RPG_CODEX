#include "RPGCalendarSubsystem.h"

#include "RPGSkyCycle.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
constexpr double SecondsPerDay = 86400.0;
constexpr int64 DaysPerMonth = 30;
constexpr int64 DaysPerYear = 360;
constexpr double MaxCalendarSeconds = 9999.0 * DaysPerYear * SecondsPerDay - 1.0;
}

UWorld* URPGCalendarSubsystem::GetTickableGameObjectWorld() const
{
    return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

bool URPGCalendarSubsystem::IsTickable() const
{
    const UWorld* World = GetTickableGameObjectWorld();
    return !IsTemplate() && World && World->IsGameWorld() && !World->bIsTearingDown && !UGameplayStatics::IsGamePaused(World);
}

TStatId URPGCalendarSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(URPGCalendarSubsystem, STATGROUP_Tickables);
}

void URPGCalendarSubsystem::Deinitialize()
{
    PrimarySky.Reset();
    OnCalendarChanged.Clear();
    Super::Deinitialize();
}

void URPGCalendarSubsystem::RegisterSky(ARPGSkyCycle* Sky)
{
    if (!IsValid(Sky)) return;
    if (PrimarySky.IsValid() && PrimarySky->GetWorld() == Sky->GetWorld() && PrimarySky.Get() != Sky) return;
    PrimarySky = Sky;
    if (!bInitialized)
    {
        const double Hours = FMath::IsFinite(Sky->TimeOfDay) ? FMath::Fmod(FMath::Fmod(double(Sky->TimeOfDay), 24.0) + 24.0, 24.0) : 17.3;
        ElapsedGameSeconds = double(FMath::RoundToInt64(Hours * 3600.0));
        bInitialized = true;
    }
    SyncSky();
    NotifyIfMinuteChanged();
}

void URPGCalendarSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetTickableGameObjectWorld();
    if (!World || !World->IsGameWorld() || UGameplayStatics::IsGamePaused(World)) return;
    if (!PrimarySky.IsValid() || PrimarySky->GetWorld() != World)
    {
        PrimarySky.Reset();
        for (TActorIterator<ARPGSkyCycle> It(World); It; ++It) { RegisterSky(*It); break; }
    }
    if (!bInitialized)
    {
        ElapsedGameSeconds = 17.3 * 3600.0;
        bInitialized = true;
    }
    // Existing frozen preview/test skies remain frozen; maps without a sky still have a calendar.
    if (!PrimarySky.IsValid() || PrimarySky->bAnimate) AdvanceRealSeconds(DeltaTime);
    else SyncSky();
}

void URPGCalendarSubsystem::AdvanceRealSeconds(double Seconds)
{
    if (!FMath::IsFinite(Seconds) || Seconds < 0.0) return;
    const double Length = FMath::IsFinite(RealMinutesPerDay) ? FMath::Max(0.1, double(RealMinutesPerDay)) : 120.0;
    SetElapsedGameSeconds(FMath::Min(MaxCalendarSeconds, ElapsedGameSeconds + Seconds * SecondsPerDay / (Length * 60.0)));
}

bool URPGCalendarSubsystem::AdvanceGameHours(double Hours)
{
    if (!FMath::IsFinite(Hours) || Hours < 0.0) return false;
    return SetElapsedGameSeconds(ElapsedGameSeconds + Hours * 3600.0);
}

bool URPGCalendarSubsystem::SetElapsedGameSeconds(double Seconds)
{
    if (!FMath::IsFinite(Seconds) || Seconds < 0.0 || Seconds > MaxCalendarSeconds) return false;
    ElapsedGameSeconds = Seconds;
    bInitialized = true;
    SyncSky();
    NotifyIfMinuteChanged();
    return true;
}

void URPGCalendarSubsystem::SetTimeOfDay(float Hours)
{
    if (!FMath::IsFinite(Hours)) return;
    const double Wrapped = FMath::Fmod(FMath::Fmod(double(Hours), 24.0) + 24.0, 24.0);
    const double DayStart = FMath::FloorToDouble(ElapsedGameSeconds / SecondsPerDay) * SecondsPerDay;
    SetElapsedGameSeconds(DayStart + FMath::Min(86399.0, double(FMath::RoundToInt64(Wrapped * 3600.0))));
}

void URPGCalendarSubsystem::SyncSky()
{
    if (PrimarySky.IsValid())
    {
        PrimarySky->DayLengthMinutes = RealMinutesPerDay;
        PrimarySky->ApplyCalendarTime(GetTimeOfDay());
    }
}

void URPGCalendarSubsystem::NotifyIfMinuteChanged()
{
    const int64 Minute = int64(FMath::FloorToDouble(ElapsedGameSeconds / 60.0));
    if (Minute != LastNotifiedMinute)
    {
        LastNotifiedMinute = Minute;
        OnCalendarChanged.Broadcast(GetDateTime());
    }
}

FRPGCalendarDateTime URPGCalendarSubsystem::FromElapsedSeconds(double Seconds)
{
    FRPGCalendarDateTime Result;
    if (!FMath::IsFinite(Seconds)) return Result;
    const int64 Whole = int64(FMath::FloorToDouble(FMath::Clamp(Seconds, 0.0, MaxCalendarSeconds)));
    const int64 DayIndex = Whole / int64(SecondsPerDay);
    const int64 DayOfYear = DayIndex % DaysPerYear;
    Result.ElapsedDays = DayIndex;
    Result.Year = int32(DayIndex / DaysPerYear) + 1;
    Result.Month = int32(DayOfYear / DaysPerMonth) + 1;
    Result.Day = int32(DayOfYear % DaysPerMonth) + 1;
    Result.Weekday = int32(DayIndex % 7) + 1;
    Result.WeekOfYear = int32(DayOfYear / 7) + 1;
    const int64 InDay = Whole % int64(SecondsPerDay);
    Result.Hour = int32(InDay / 3600);
    Result.Minute = int32((InDay / 60) % 60);
    Result.Second = int32(InDay % 60);
    return Result;
}

FRPGCalendarDateTime URPGCalendarSubsystem::GetDateTime() const { return FromElapsedSeconds(ElapsedGameSeconds); }
FRPGCalendarDateTime URPGCalendarSubsystem::GetDateTimeAfterHours(double Hours) const
{
    return FromElapsedSeconds(ElapsedGameSeconds + (FMath::IsFinite(Hours) ? FMath::Max(0.0, Hours) * 3600.0 : 0.0));
}
float URPGCalendarSubsystem::GetTimeOfDay() const { return float(FMath::Fmod(ElapsedGameSeconds, SecondsPerDay) / 3600.0); }

FText URPGCalendarSubsystem::FormatDate(const FRPGCalendarDateTime& Value) const
{
    const FString Month = MonthNames.IsValidIndex(Value.Month - 1) && !MonthNames[Value.Month - 1].IsEmpty()
        ? MonthNames[Value.Month - 1] : FString::Printf(TEXT("Month %d"), Value.Month);
    return FText::FromString(FString::Printf(TEXT("Day %d, %s, Year %d"), Value.Day, *Month, Value.Year));
}
FText URPGCalendarSubsystem::FormatTime(const FRPGCalendarDateTime& Value) const
{
    return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Value.Hour, Value.Minute));
}
FText URPGCalendarSubsystem::FormatWeek(const FRPGCalendarDateTime& Value) const
{
    const FString Weekday = WeekdayNames.IsValidIndex(Value.Weekday - 1) && !WeekdayNames[Value.Weekday - 1].IsEmpty()
        ? WeekdayNames[Value.Weekday - 1] : FString::Printf(TEXT("Weekday %d"), Value.Weekday);
    return FText::FromString(FString::Printf(TEXT("%s  |  Week %d"), *Weekday, Value.WeekOfYear));
}
