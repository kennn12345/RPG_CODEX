#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGWeatherSubsystem.h"
#include "RPGSkyCycle.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyLight;
class UMaterialParameterCollection;
class URPGWeatherEffectsComponent;
class UMaterialInstanceDynamic;
class UVolumetricCloudComponent;
class URPGGroundFogComponent;
class URPGRainSurfaceComponent;

// One continuous day/night cycle (approved sky concept: large sun and moon, stars + Milky Way at night, gradual
// morning / noon / sunset / night). Moves the real Sun and Moon directional lights (both lighting the sky atmosphere:
// Sun = atmosphere light 0, Moon = 1), and blends sky light, haze colour and exposure with the sun's height.
// The sky dome, mist and fireflies read sun / moon direction and the night amount from custom primitive data (SkyReceivers).
// Edit TimeOfDay in the details panel to preview any hour; in game it advances when bAnimate is on.
UCLASS()
class RPG_CODEX_API ARPGSkyCycle : public AActor
{
	GENERATED_BODY()

public:
	ARPGSkyCycle();
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Weather") TObjectPtr<URPGWeatherEffectsComponent> WeatherEffects;
	UPROPERTY(VisibleAnywhere,Category="Weather") TObjectPtr<URPGGroundFogComponent> GroundFog;
	UPROPERTY(VisibleAnywhere,Category="Weather") TObjectPtr<URPGRainSurfaceComponent> RainSurfaces;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sky|Preview",meta=(ClampMin="1",ClampMax="30")) int32 PreviewDayOfMonth=30;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sky|Preview") ERPGWeatherType PreviewWeather=ERPGWeatherType::Clear;
	UFUNCTION(BlueprintPure,Category="Sky") float GetMoonPhase() const;
	UFUNCTION(BlueprintPure,Category="Sky") float GetMoonIllumination() const;
	UFUNCTION(BlueprintPure,Category="Sky") FVector GetMoonDirection() const;
	UFUNCTION(BlueprintPure,Category="Weather") FRPGWeatherState GetWeatherState() const;
	UFUNCTION(BlueprintCallable,Category="Sky|Preview") void RefreshSky() { Apply(); }
	FVector GetCloudWindOffset()const{return CloudWindOffset;}
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	// Hours, 0..24 (6 = sunrise, 12 = noon, 18 = sunset, 0 = midnight).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0", ClampMax = "24"))
	float TimeOfDay = 17.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	bool bAnimate = true;

	// Full cycle duration. During play the shared calendar supplies this value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0.1"))
	float DayLengthMinutes = 120.f;

	// Compass direction (world yaw) where the sun rises; it sets opposite.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	float SunriseYaw = 140.f;

	// How far the noon sun is tilted from straight overhead (degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (ClampMin = "0", ClampMax = "80"))
	float NoonTilt = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<ADirectionalLight> Sun;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<ADirectionalLight> Moon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<AExponentialHeightFog> Fog;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<APostProcessVolume> Grade;

	// Optional: scalars written every update: NightAmount (0 day .. 1 night), SunHeight (-1..1), TimeOfDay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TObjectPtr<UMaterialParameterCollection> SkyParams;

	// Actors whose materials follow the cycle (sky dome, mist, fireflies): every primitive gets custom primitive data
	// 0-2 = direction to the sun, 3-5 = direction to the moon, 6 = night amount, 7 = sun height. (The engine's
	// SkyAtmosphereLightDirection node falls back to "straight up" while a light is off, so it can't tell night.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Actors")
	TArray<TObjectPtr<AActor>> SkyReceivers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	float SunIntensity = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	FLinearColor SunColorNoon = FLinearColor(1.f, 0.95f, 0.88f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	FLinearColor SunColorLow = FLinearColor(1.f, 0.62f, 0.36f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	float MoonIntensity = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sky|Light") float MoonLightScale=.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.66f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	float SkyLightDay = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Light")
	float SkyLightNight = 2.5f;

	// Distance haze: noon, low sun (morning / sunset) and night.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Haze")
	FLinearColor HazeDay = FLinearColor(0.33f, 0.56f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Haze")
	FLinearColor HazeLow = FLinearColor(0.62f, 0.46f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Haze")
	FLinearColor HazeNight = FLinearColor(0.012f, 0.02f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Exposure")
	float ExposureBiasDay = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky|Exposure")
	float ExposureBiasNight = -0.6f;

	UFUNCTION(BlueprintCallable, Category = "Sky")
	void SetTimeOfDay(float Hours);

	// Calendar receiver; unlike SetTimeOfDay, does not write back into the clock.
	void ApplyCalendarTime(float Hours);

	// Unit vector toward the sun at the current time (the moon is opposite).
	UFUNCTION(BlueprintPure, Category = "Sky")
	FVector GetSunDirection() const;

	// 0 = full day .. 1 = full night.
	UFUNCTION(BlueprintPure, Category = "Sky")
	float GetNightAmount() const;

private:
	void Apply();
	FVector CloudWindOffset=FVector::ZeroVector;
	double CalendarSeconds() const;
	bool bCachedFog=false;
	float BaseFogDensity=.007f,BaseFogFalloff=.1f,BaseFogStart=2500,BaseFogMaxOpacity=.8f;
	float BaseVolumeDistance=10000,BaseVolumeStart=0,BaseVolumeFade=0;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> WaterLighting;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
	UPROPERTY(Transient) TObjectPtr<UVolumetricCloudComponent> CloudVolume;
};
