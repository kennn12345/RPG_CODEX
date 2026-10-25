#pragma once
#include "CoreMinimal.h"

/** Animation-worker snapshot of the spring-driven lower body's turn lag. */
struct FRPGFlightRearSway
{
    FVector WaistRotationCS = FVector::ZeroVector;
    float KneeFlexion = 0.f; // radians, anatomical knee hinge only
};
// Shared flight-only clip order for the component and animation proxy.
namespace RPGFlight
{
    enum EClip : int32
    {
        Hover, Forward, Fast, Brake,
        BankL, BankR, BankFastL, BankFastR,
        Takeoff, Accelerate, Boost, Release,
        NumClips
    };
}
