#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGWeatherSubsystem.generated.h"

UENUM(BlueprintType)
enum class ERPGWeatherType : uint8 { Clear, Haze, Cloudy, Fog, Rain, Storm };

UENUM(BlueprintType)
enum class ERPGCloudStyle:uint8 { Automatic, Clear, Scattered, Billowing, Broken, Overcast, StormFront };

USTRUCT(BlueprintType)
struct RPG_CODEX_API FRPGWeatherState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) ERPGWeatherType Type=ERPGWeatherType::Clear;
    UPROPERTY(BlueprintReadOnly) float Clouds=0;
    UPROPERTY(BlueprintReadOnly) float Haze=0;
    UPROPERTY(BlueprintReadOnly) float Fog=0;
    UPROPERTY(BlueprintReadOnly) float Rain=0;
    UPROPERTY(BlueprintReadOnly) float Wind=0.1f;
    UPROPERTY(BlueprintReadOnly) float Storm=0;
    UPROPERTY(BlueprintReadOnly) float WindYawDegrees=45;
    UPROPERTY(BlueprintReadOnly) ERPGCloudStyle CloudStyle=ERPGCloudStyle::Clear;
    UPROPERTY(BlueprintReadOnly) float Puffs=1;
    UPROPERTY(BlueprintReadOnly) float Billows=0;
    UPROPERTY(BlueprintReadOnly) float Broken=0;
    UPROPERTY(BlueprintReadOnly) float Layers=0;
    UPROPERTY(BlueprintReadOnly) float Shelf=0;
    FVector WindVelocity()const;
};

/** Clock-derived weather, independent of months/seasons, shared across level travel. */
UCLASS(Config=Game)
class RPG_CODEX_API URPGWeatherSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,BlueprintReadWrite,Category="Weather") int32 WeatherSeed=7319;
    /** Direction the air travels toward: +X east=0, +Y north=90. */
    UPROPERTY(Config,EditAnywhere,BlueprintReadWrite,Category="Weather") float PrevailingWindYawDegrees=45;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weather") bool bRainEffects=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weather") bool bLightning=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weather") bool bWeatherAudio=true;
    UFUNCTION(BlueprintPure,Category="Weather") FRPGWeatherState GetCurrentWeather() const;
    UFUNCTION(BlueprintPure,Category="Weather") bool IsWeatherOverridden() const { return bOverride; }
    UFUNCTION(BlueprintCallable,Category="Weather") void SetWeatherOverride(ERPGWeatherType Type,float TransitionRealSeconds=8.f);
    UFUNCTION(BlueprintCallable,Category="Weather") void ResumeAutomaticWeather(float TransitionRealSeconds=8.f);
    UFUNCTION(BlueprintCallable,Category="Weather") void PreviewCloudStyle(ERPGCloudStyle Style,float TransitionRealSeconds=8.f);
    UFUNCTION(BlueprintCallable,Category="Weather") void SetWindDirection(float TowardYawDegrees,float TransitionRealSeconds=8.f);
    UFUNCTION(BlueprintCallable,Category="Weather") void ResumeAutomaticWind(float TransitionRealSeconds=8.f);
    UFUNCTION(BlueprintPure,Category="Weather") bool IsWindOverridden()const{return bWindOverride;}
    UFUNCTION(BlueprintPure,Category="Weather") static FString CloudName(ERPGCloudStyle Style);
    UFUNCTION(BlueprintPure,Category="Weather") static FString WeatherName(ERPGWeatherType Type);
    static FRPGWeatherState Preset(ERPGWeatherType Type);
    static FRPGWeatherState NaturalWeather(double GameSeconds,int32 Seed,float PrevailingYaw=45.f);
    static FRPGWeatherState Blend(const FRPGWeatherState& A,const FRPGWeatherState& B,float Alpha);
    static void SetCloudStyle(FRPGWeatherState& State,ERPGCloudStyle Style);
    /** Phase 0 is full at midnight of day 30; 0.5 is new on day 15. */
    static float MoonPhase(double GameSeconds);
    static float MoonIllumination(double GameSeconds);
private:
    double ClockSeconds() const;
    void StartTransition(bool bForce,ERPGWeatherType Type,float RealSeconds,ERPGCloudStyle Style=ERPGCloudStyle::Automatic);
    double RealToGameSeconds(float RealSeconds)const;
    bool bOverride=false;
    ERPGWeatherType OverrideType=ERPGWeatherType::Clear;
    FRPGWeatherState TransitionFrom;
    double TransitionStart=0,TransitionDuration=0;
    ERPGCloudStyle CloudOverride=ERPGCloudStyle::Automatic;
    bool bWindOverride=false;
    float WindFrom=45,WindTarget=45;
    double WindStart=0,WindDuration=0;
};
