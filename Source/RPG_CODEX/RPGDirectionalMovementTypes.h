#pragma once
#include "CoreMinimal.h"

/** Animation-worker snapshot. Four directional hover poses blend continuously through the origin. */
struct FRPGDirectionalHoverPose
{
    float Weight = 0.f;
    float Forward = 0.f;
    float Right = 0.f;
    float Bob = 0.f;
    float SideKneeLift = 30.f;
    float SideKneeBend = 46.f;
    float SideKneeOut = 22.f;
    float SideHipOpen = 40.f;
    float SideFootTuck = 14.f;
    FVector2D FootTuck = FVector2D::ZeroVector;
    float TrailingArmMinOut = 8.f;
    float TrailingArmSpacing = 5.08f;
    float SideBodyLean = 12.f;
    float SideChestCounterLean = 2.f;
    float SideArmLean = 14.f;
    float ArmSide = 0.f;
    FVector2D KneeOut = FVector2D::ZeroVector; // left / right delayed outward drift
    float Turn = 0.f;
    FVector UpCS = FVector::UpVector;
    FVector RightCS = -FVector::XAxisVector;
};
