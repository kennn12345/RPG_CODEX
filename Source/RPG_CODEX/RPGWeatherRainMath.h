#pragma once
#include "CoreMinimal.h"

namespace RPGRain
{
constexpr double MinimumHeight=-600,MaximumHeight=1600;
inline double Wrap(double Value,double Minimum,double Maximum)
{
    const double Range=Maximum-Minimum;
    return Minimum+FMath::Fmod(FMath::Fmod(Value-Minimum,Range)+Range,Range);
}
inline FVector WrapLocal(const FVector& P,double Radius)
{
    return FVector(Wrap(P.X,-Radius,Radius),Wrap(P.Y,-Radius,Radius),Wrap(P.Z,MinimumHeight,MaximumHeight));
}
inline FVector Velocity(float Wind,float Speed,float YawDegrees=45.f)
{
    // Wind alone determines the world-space fall direction, never camera velocity.
    const float Yaw=FMath::DegreesToRadians(YawDegrees),Drift=FMath::Lerp(200.f,1200.f,Wind)*.12f;
    return FVector(FMath::Cos(Yaw)*Drift,FMath::Sin(Yaw)*Drift,-Speed);
}
}
