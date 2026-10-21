#include "RPGHitCameraShake.h"

#include "Shakes/PerlinNoiseCameraShakePattern.h"

URPGHitCameraShake::URPGHitCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Fast, mostly rotational noise that dies out quickly: a jolt, not a wobble.
	UPerlinNoiseCameraShakePattern* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("HitShakePattern"));
	Pattern->Duration = 0.18f;
	Pattern->BlendInTime = 0.f;
	Pattern->BlendOutTime = 0.12f;
	Pattern->Pitch.Amplitude = 1.2f;
	Pattern->Pitch.Frequency = 30.f;
	Pattern->Yaw.Amplitude = 0.8f;
	Pattern->Yaw.Frequency = 25.f;
	Pattern->Roll.Amplitude = 0.6f;
	Pattern->Roll.Frequency = 20.f;
	Pattern->X.Amplitude = 2.f;
	Pattern->X.Frequency = 30.f;
	SetRootShakePattern(Pattern);
}
