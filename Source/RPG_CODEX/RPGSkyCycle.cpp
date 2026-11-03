#include "RPGSkyCycle.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

ARPGSkyCycle::ARPGSkyCycle()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ARPGSkyCycle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Apply();
}

void ARPGSkyCycle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAnimate) return;
	TimeOfDay = FMath::Fmod(TimeOfDay + DeltaSeconds * 24.f / (DayLengthMinutes * 60.f), 24.f);
	Apply();
}

void ARPGSkyCycle::SetTimeOfDay(float Hours)
{
	TimeOfDay = FMath::Fmod(FMath::Fmod(Hours, 24.f) + 24.f, 24.f);
	Apply();
}

FVector ARPGSkyCycle::GetSunDirection() const
{
	// The sun turns on a circle: rises at SunriseYaw (6 h), peaks NoonTilt from overhead (12 h), sets opposite (18 h).
	const float Theta = (TimeOfDay - 6.f) / 24.f * 2.f * PI;
	const FVector East = FRotator(0.f, SunriseYaw, 0.f).Vector();
	const FVector Side = FRotator(0.f, SunriseYaw + 90.f, 0.f).Vector();
	const float Tilt = FMath::DegreesToRadians(NoonTilt);
	const FVector Up = FVector::UpVector * FMath::Cos(Tilt) + Side * FMath::Sin(Tilt);
	return (East * FMath::Cos(Theta) + Up * FMath::Sin(Theta)).GetSafeNormal();
}

float ARPGSkyCycle::GetNightAmount() const
{
	return 1.f - FMath::SmoothStep(-0.16f, 0.06f, float(GetSunDirection().Z));
}

void ARPGSkyCycle::Apply()
{
	const FVector SunDir = GetSunDirection();
	const float Height = SunDir.Z;
	const float Night = GetNightAmount();
	const float Low = 1.f - FMath::SmoothStep(0.04f, 0.42f, Height);        // 1 near the horizon (morning / sunset)

	if (Sun)
	{
		// Light travels away from the sun. The atmosphere reddens and finally blocks it near/below the horizon.
		Sun->SetActorRotation((-SunDir).Rotation());
		if (ULightComponent* L = Sun->GetLightComponent())
		{
			L->SetIntensity(SunIntensity * FMath::SmoothStep(-0.1f, 0.02f, Height));
			L->SetLightColor(FLinearColor::LerpUsingHSV(SunColorNoon, SunColorLow, Low));
		}
	}
	if (Moon)
	{
		Moon->SetActorRotation(SunDir.Rotation());
		if (ULightComponent* L = Moon->GetLightComponent())
		{
			L->SetIntensity(MoonIntensity * Night * FMath::SmoothStep(-0.05f, 0.08f, -Height));
			L->SetLightColor(MoonColor);
		}
	}
	if (SkyLight && SkyLight->GetLightComponent())
		SkyLight->GetLightComponent()->SetIntensity(FMath::Lerp(SkyLightDay, SkyLightNight, Night));
	if (Fog && Fog->GetComponent())
	{
		const FLinearColor DayHaze = FMath::Lerp(HazeDay, HazeLow, Low);
		Fog->GetComponent()->SetFogInscatteringColor(FMath::Lerp(DayHaze, HazeNight, Night));
		Fog->GetComponent()->SetDirectionalInscatteringColor(FLinearColor(0.9f, 0.62f, 0.35f) * (1.f - Night) * (0.25f + 0.75f * Low));
	}
	if (Grade)
	{
		Grade->Settings.bOverride_AutoExposureBias = true;
		Grade->Settings.AutoExposureBias = FMath::Lerp(ExposureBiasDay, ExposureBiasNight, Night);
	}
	const float Data[8] = { float(SunDir.X), float(SunDir.Y), float(SunDir.Z), float(-SunDir.X), float(-SunDir.Y), float(-SunDir.Z), Night, Height };
	const bool bGame = GetWorld() && GetWorld()->IsGameWorld();
	for (AActor* Receiver : SkyReceivers)
	{
		if (!Receiver) continue;
		TInlineComponentArray<UPrimitiveComponent*> Prims(Receiver);
		for (UPrimitiveComponent* Prim : Prims)
			for (int32 i = 0; i < 8; ++i)
			{
				// Editor: saved defaults (also what PIE starts from); game: the runtime values.
				if (bGame) Prim->SetCustomPrimitiveDataFloat(i, Data[i]);
				else Prim->SetDefaultCustomPrimitiveDataFloat(i, Data[i]);
			}
	}
	if (SkyParams && GetWorld())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, SkyParams, TEXT("NightAmount"), Night);
		UKismetMaterialLibrary::SetScalarParameterValue(this, SkyParams, TEXT("SunHeight"), Height);
		UKismetMaterialLibrary::SetScalarParameterValue(this, SkyParams, TEXT("TimeOfDay"), TimeOfDay);
	}
}
