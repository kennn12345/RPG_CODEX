#pragma once

#include "CoreMinimal.h"
#include "RPGWaterWaves.generated.h"

// Gerstner wave set shared by C++ (buoyancy, immersion) and M_StylizedWater's world position offset, so floating
// objects ride the same waves that are drawn. The material's WaveHeight/WaveLength/WaveSpeed/WaveDirection parameters
// must match the zone's settings (Tools/Water/build_water_test.py writes both). Four waves, fixed ratios.
USTRUCT(BlueprintType)
struct FRPGWaveSettings
{
	GENERATED_BODY()

	// Height of the largest wave (cm). 0 = flat water.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
	float Height = 0.f;

	// Wavelength of the largest wave (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
	float Length = 1200.f;

	// Multiplier on deep-water wave speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
	float Speed = 1.f;

	// Travel direction of the largest wave (degrees, world yaw).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waves")
	float Direction = 0.f;
};

namespace RPGWaterWaves
{
	// Wave i: direction offset (deg), wavelength ratio, height ratio. Mirrors the HLSL in build_water_test.py.
	constexpr float Dir[4] = { 0.f, 35.f, -28.f, 70.f };
	constexpr float Len[4] = { 1.f, 0.62f, 0.41f, 0.27f };
	constexpr float Amp[4] = { 1.f, 0.55f, 0.35f, 0.2f };

	// Surface height offset (cm) at world XY and time T (s); ignores the small horizontal Gerstner shift.
	inline float Height(const FRPGWaveSettings& W, const FVector2D& XY, float T)
	{
		if (W.Height <= 0.f) return 0.f;
		float Z = 0.f;
		for (int32 i = 0; i < 4; ++i)
		{
			const float A = FMath::DegreesToRadians(W.Direction + Dir[i]);
			const FVector2D D(FMath::Cos(A), FMath::Sin(A));
			const float K = 2.f * PI / FMath::Max(W.Length * Len[i], 1.f);
			const float Omega = FMath::Sqrt(980.f * K) * W.Speed;
			Z += W.Height * Amp[i] * FMath::Sin(K * FVector2D::DotProduct(D, XY) - Omega * T);
		}
		return Z;
	}
}
