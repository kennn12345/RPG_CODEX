#include "RPGWeatherSubsystem.h"
#include "RPGCalendarSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
uint32 WeatherHash(uint32 X){X^=X>>16;X*=0x7feb352d;X^=X>>15;X*=0x846ca68b;X^=X>>16;return X;}
FRPGWeatherState SlotWeather(int64 Slot,int32 Seed,float PrevailingYaw)
{
    const uint32 Roll=WeatherHash(uint32(Slot)^WeatherHash(uint32(Seed)))%100;
    auto S=URPGWeatherSubsystem::Preset(Roll<32?ERPGWeatherType::Clear:Roll<47?ERPGWeatherType::Haze:Roll<67?ERPGWeatherType::Cloudy:Roll<77?ERPGWeatherType::Fog:Roll<93?ERPGWeatherType::Rain:ERPGWeatherType::Storm);
    const uint32 Shape=WeatherHash(uint32(Slot)+uint32(Seed)*17+911)%100;
    if(S.Type==ERPGWeatherType::Cloudy)URPGWeatherSubsystem::SetCloudStyle(S,Shape<25?ERPGCloudStyle::Scattered:Shape<50?ERPGCloudStyle::Billowing:Shape<75?ERPGCloudStyle::Broken:ERPGCloudStyle::Overcast);
    if(S.Type==ERPGWeatherType::Clear && Shape<35)URPGWeatherSubsystem::SetCloudStyle(S,ERPGCloudStyle::Scattered);
    S.WindYawDegrees=FMath::UnwindDegrees(PrevailingYaw+(float(WeatherHash(uint32(Slot)+uint32(Seed)*31)%1000)/999.f-.5f)*(S.Storm>.5f?90.f:50.f));
    return S;
}
}
FVector FRPGWeatherState::WindVelocity()const
{
    const float Angle=FMath::DegreesToRadians(WindYawDegrees);
    return FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*FMath::Lerp(200.f,1200.f,Wind);
}
void URPGWeatherSubsystem::SetCloudStyle(FRPGWeatherState& S,ERPGCloudStyle Style)
{
    S.CloudStyle=Style;S.Puffs=S.Billows=S.Broken=S.Layers=S.Shelf=0;
    switch(Style)
    {
    case ERPGCloudStyle::Scattered:S.Puffs=1;S.Clouds=.35f;break;
    case ERPGCloudStyle::Billowing:S.Billows=1;S.Clouds=.55f;break;
    case ERPGCloudStyle::Broken:S.Broken=1;S.Clouds=.65f;break;
    case ERPGCloudStyle::Overcast:S.Layers=1;S.Clouds=.97f;break;
    case ERPGCloudStyle::StormFront:S.Shelf=1;S.Clouds=1;break;
    default:S.Puffs=1;S.Clouds=0;break;
    }
}
FRPGWeatherState URPGWeatherSubsystem::Preset(ERPGWeatherType Type)
{
    FRPGWeatherState S;S.Type=Type;
    switch(Type)
    {
    case ERPGWeatherType::Haze:SetCloudStyle(S,ERPGCloudStyle::Clear);S.Clouds=.08f;S.Haze=.65f;S.Wind=.15f;break;
    case ERPGWeatherType::Cloudy:SetCloudStyle(S,ERPGCloudStyle::Broken);S.Haze=.18f;S.Wind=.4f;break;
    case ERPGWeatherType::Fog:SetCloudStyle(S,ERPGCloudStyle::Broken);S.Clouds=.3f;S.Haze=.5f;S.Fog=.85f;S.Wind=.08f;break;
    case ERPGWeatherType::Rain:SetCloudStyle(S,ERPGCloudStyle::Overcast);S.Haze=.25f;S.Fog=.15f;S.Rain=.8f;S.Wind=.45f;break;
    case ERPGWeatherType::Storm:SetCloudStyle(S,ERPGCloudStyle::StormFront);S.Haze=.4f;S.Fog=.2f;S.Rain=1;S.Wind=1;S.Storm=1;break;
    default:SetCloudStyle(S,ERPGCloudStyle::Clear);break;
    }
    return S;
}
FRPGWeatherState URPGWeatherSubsystem::Blend(const FRPGWeatherState& A,const FRPGWeatherState& B,float Alpha)
{
    const float T=FMath::Clamp(Alpha,0.f,1.f);FRPGWeatherState S;S.Type=T<.5f?A.Type:B.Type;
    S.Clouds=FMath::Lerp(A.Clouds,B.Clouds,T);S.Haze=FMath::Lerp(A.Haze,B.Haze,T);
    S.Fog=FMath::Lerp(A.Fog,B.Fog,T);
    // Cloud cover develops first; precipitation starts only beneath a substantial deck.
    const float RainT=A.Rain<B.Rain?FMath::SmoothStep(.45f,.95f,T):FMath::SmoothStep(0.f,.55f,T);
    S.Rain=FMath::Lerp(A.Rain,B.Rain,RainT)*FMath::SmoothStep(.72f,.9f,S.Clouds);
    S.Wind=FMath::Lerp(A.Wind,B.Wind,T);S.Storm=FMath::Lerp(A.Storm,B.Storm,T);
    S.WindYawDegrees=FMath::UnwindDegrees(A.WindYawDegrees+FMath::FindDeltaAngleDegrees(A.WindYawDegrees,B.WindYawDegrees)*T);
    S.CloudStyle=T<.5f?A.CloudStyle:B.CloudStyle;
    S.Puffs=FMath::Lerp(A.Puffs,B.Puffs,T);S.Billows=FMath::Lerp(A.Billows,B.Billows,T);S.Broken=FMath::Lerp(A.Broken,B.Broken,T);
    S.Layers=FMath::Lerp(A.Layers,B.Layers,T);S.Shelf=FMath::Lerp(A.Shelf,B.Shelf,T);return S;
}
FRPGWeatherState URPGWeatherSubsystem::NaturalWeather(double Seconds,int32 Seed,float PrevailingYaw)
{
    if(!FMath::IsFinite(Seconds))Seconds=0;
    Seconds=FMath::Max(0.0,Seconds);
    constexpr double SlotLength=6*3600.0;
    const int64 Slot=int64(FMath::FloorToDouble(Seconds/SlotLength));
    // A weather front develops over the first game hour (five real minutes at the default pace).
    const float T=FMath::Clamp(float(FMath::Fmod(Seconds,SlotLength)/3600.0),0.f,1.f);
    return Blend(SlotWeather(Slot-1,Seed,PrevailingYaw),SlotWeather(Slot,Seed,PrevailingYaw),T*T*(3-2*T));
}
double URPGWeatherSubsystem::ClockSeconds() const
{
    const auto* C=GetGameInstance()?GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>():nullptr;
    return C?C->GetElapsedGameSeconds():0;
}
FRPGWeatherState URPGWeatherSubsystem::GetCurrentWeather() const
{
    const double Now=ClockSeconds();
    auto Target=bOverride?Preset(OverrideType):NaturalWeather(Now,WeatherSeed,PrevailingWindYawDegrees);
    if(bOverride){Target.WindYawDegrees=PrevailingWindYawDegrees;if(CloudOverride!=ERPGCloudStyle::Automatic)SetCloudStyle(Target,CloudOverride);}
    // Developer date/time previews can rewind the clock; never strand a front in the future.
    const float T=TransitionDuration<=0 || Now<TransitionStart?1.f:FMath::Clamp(float((Now-TransitionStart)/TransitionDuration),0.f,1.f);
    auto S=Blend(TransitionFrom,Target,T*T*(3-2*T));
    const float WindTo=bWindOverride?WindTarget:S.WindYawDegrees;
    const float WT=WindDuration<=0 || Now<WindStart?1.f:FMath::Clamp(float((Now-WindStart)/WindDuration),0.f,1.f);
    S.WindYawDegrees=FMath::UnwindDegrees(WindFrom+FMath::FindDeltaAngleDegrees(WindFrom,WindTo)*WT*WT*(3-2*WT));
    return S;
}
double URPGWeatherSubsystem::RealToGameSeconds(float RealSeconds)const
{
    const auto* C=GetGameInstance()?GetGameInstance()->GetSubsystem<URPGCalendarSubsystem>():nullptr;
    const double Rate=C?86400.0/(FMath::Max(.1f,C->RealMinutesPerDay)*60.0):12;
    return FMath::IsFinite(RealSeconds)?FMath::Clamp(double(RealSeconds),0.0,120.0)*Rate:0;
}
void URPGWeatherSubsystem::StartTransition(bool bForce,ERPGWeatherType Type,float RealSeconds,ERPGCloudStyle Style)
{
    TransitionFrom=GetCurrentWeather();TransitionStart=ClockSeconds();TransitionDuration=RealToGameSeconds(RealSeconds);
    bOverride=bForce;OverrideType=Type;CloudOverride=Style;
}
void URPGWeatherSubsystem::SetWeatherOverride(ERPGWeatherType Type,float Seconds){StartTransition(true,Type,Seconds);}
void URPGWeatherSubsystem::ResumeAutomaticWeather(float Seconds){StartTransition(false,ERPGWeatherType::Clear,Seconds);}
void URPGWeatherSubsystem::PreviewCloudStyle(ERPGCloudStyle Style,float Seconds){StartTransition(true,Style==ERPGCloudStyle::Clear?ERPGWeatherType::Clear:ERPGWeatherType::Cloudy,Seconds,Style);}
void URPGWeatherSubsystem::SetWindDirection(float Yaw,float Seconds)
{
    WindFrom=GetCurrentWeather().WindYawDegrees;WindTarget=FMath::UnwindDegrees(FMath::IsFinite(Yaw)?Yaw:45.f);
    WindStart=ClockSeconds();WindDuration=RealToGameSeconds(Seconds);bWindOverride=true;
}
void URPGWeatherSubsystem::ResumeAutomaticWind(float Seconds)
{
    WindFrom=GetCurrentWeather().WindYawDegrees;WindStart=ClockSeconds();WindDuration=RealToGameSeconds(Seconds);bWindOverride=false;
}
FString URPGWeatherSubsystem::CloudName(ERPGCloudStyle Style)
{
    switch(Style){case ERPGCloudStyle::Scattered:return TEXT("Scattered puffs");case ERPGCloudStyle::Billowing:return TEXT("Tall billows");case ERPGCloudStyle::Broken:return TEXT("Broken clouds");case ERPGCloudStyle::Overcast:return TEXT("Heavy overcast");case ERPGCloudStyle::StormFront:return TEXT("Storm deck");case ERPGCloudStyle::Clear:return TEXT("Clear breaks");default:return TEXT("Automatic");}
}
float URPGWeatherSubsystem::MoonPhase(double Seconds)
{
    if(!FMath::IsFinite(Seconds))return 0;
    const double Days=FMath::Max(0.0,Seconds)/86400.0;
    return float(FMath::Fmod(Days+1.0,30.0)/30.0);
}
float URPGWeatherSubsystem::MoonIllumination(double Seconds){return .5f+.5f*FMath::Cos(2*PI*MoonPhase(Seconds));}
FString URPGWeatherSubsystem::WeatherName(ERPGWeatherType Type)
{
    switch(Type){case ERPGWeatherType::Haze:return TEXT("Haze");case ERPGWeatherType::Cloudy:return TEXT("Cloudy");case ERPGWeatherType::Fog:return TEXT("Fog");case ERPGWeatherType::Rain:return TEXT("Rain");case ERPGWeatherType::Storm:return TEXT("Storm");default:return TEXT("Clear");}
}

