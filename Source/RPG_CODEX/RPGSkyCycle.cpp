#include "RPGSkyCycle.h"
#include "RPGCalendarSubsystem.h"
#include "RPGWeatherEffectsComponent.h"
#include "RPGGroundFogComponent.h"
#include "RPGRainSurfaceComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

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
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/VolumetricCloudComponent.h"

ARPGSkyCycle::ARPGSkyCycle()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	WeatherEffects=CreateDefaultSubobject<URPGWeatherEffectsComponent>(TEXT("WeatherEffects"));
	GroundFog=CreateDefaultSubobject<URPGGroundFogComponent>(TEXT("GroundFog"));
	RainSurfaces=CreateDefaultSubobject<URPGRainSurfaceComponent>(TEXT("RainSurfaces"));
}

void ARPGSkyCycle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Apply();
}

void ARPGSkyCycle::BeginPlay()
{
	Super::BeginPlay();
	// Reuse the engine's genuinely volumetric clouds, controlled by the weather front.
	for(TActorIterator<AActor> It(GetWorld());It;++It)
		if(auto* Volume=It->FindComponentByClass<UVolumetricCloudComponent>())
		{
			CloudVolume=Volume;It->SetActorHiddenInGame(false);
			UMaterialInterface* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RPG/Environment/Weather/MI_Weather_Clouds.MI_Weather_Clouds"));
			if(!Parent)Parent=Volume->GetMaterial();if(!Parent)Parent=Volume->Material.LoadSynchronous();
			if(Parent){CloudMaterial=UMaterialInstanceDynamic::Create(Parent,this);Volume->SetMaterial(CloudMaterial);}
			Volume->SetLayerBottomAltitude(1.1f);Volume->SetLayerHeight(2.8f);
			Volume->SetViewSampleCountScale(2.5f);break;
		}
	if(Sun)if(auto* Light=Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		Light->CloudScatteredLuminanceScale=FLinearColor(2.5f,2.5f,2.5f);
		Light->MarkRenderStateDirty();
	}
	if (UGameInstance* Instance = GetGameInstance())
		if (URPGCalendarSubsystem* Calendar = Instance->GetSubsystem<URPGCalendarSubsystem>()) Calendar->RegisterSky(this);
}

void ARPGSkyCycle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	CloudWindOffset+=GetWeatherState().WindVelocity()*DeltaSeconds;
	if(CloudMaterial)CloudMaterial->SetVectorParameterValue(TEXT("WeatherCloudOffset"),FLinearColor(CloudWindOffset.X,CloudWindOffset.Y,0,0));
	// The game-instance clock advances once and drives the sky, including skipped time.
	if (UGameInstance* Instance = GetGameInstance())
		if (Instance->GetSubsystem<URPGCalendarSubsystem>()) return;
	if (!bAnimate) return;
	TimeOfDay = FMath::Fmod(TimeOfDay + DeltaSeconds * 24.f / (DayLengthMinutes * 60.f), 24.f);
	Apply();
}

void ARPGSkyCycle::SetTimeOfDay(float Hours)
{
	if (!FMath::IsFinite(Hours)) return;
	if (GetWorld() && GetWorld()->IsGameWorld())
		if (UGameInstance* Instance = GetGameInstance())
			if (URPGCalendarSubsystem* Calendar = Instance->GetSubsystem<URPGCalendarSubsystem>())
			{
				Calendar->RegisterSky(this);
				Calendar->SetTimeOfDay(Hours);
				return;
			}
	TimeOfDay = FMath::Fmod(FMath::Fmod(Hours, 24.f) + 24.f, 24.f);
	Apply();
}

void ARPGSkyCycle::ApplyCalendarTime(float Hours)
{
	TimeOfDay = Hours;
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

double ARPGSkyCycle::CalendarSeconds() const
{
	if(GetWorld() && GetWorld()->IsGameWorld())
		if(auto* GI=GetGameInstance())if(auto* C=GI->GetSubsystem<URPGCalendarSubsystem>())return C->GetElapsedGameSeconds();
	return (FMath::Clamp(PreviewDayOfMonth,1,30)-1)*86400.0+TimeOfDay*3600.0;
}
float ARPGSkyCycle::GetMoonPhase() const{return URPGWeatherSubsystem::MoonPhase(CalendarSeconds());}
float ARPGSkyCycle::GetMoonIllumination() const{return URPGWeatherSubsystem::MoonIllumination(CalendarSeconds());}
FRPGWeatherState ARPGSkyCycle::GetWeatherState() const
{
	if(GetWorld() && GetWorld()->IsGameWorld())
		if(auto* GI=GetGameInstance())if(auto* W=GI->GetSubsystem<URPGWeatherSubsystem>())return W->GetCurrentWeather();
	return URPGWeatherSubsystem::Preset(PreviewWeather);
}
FVector ARPGSkyCycle::GetMoonDirection() const
{
	// Full moon is opposite the sun. Waning quarters rise later; waxing quarters appear earlier.
	const float Theta=(TimeOfDay+12.f-GetMoonPhase()*24.f-6.f)/24.f*2*PI;
	const FVector East=FRotator(0,SunriseYaw,0).Vector(),Side=FRotator(0,SunriseYaw+90,0).Vector();
	const float Tilt=FMath::DegreesToRadians(NoonTilt);
	return (East*FMath::Cos(Theta)+(FVector::UpVector*FMath::Cos(Tilt)+Side*FMath::Sin(Tilt))*FMath::Sin(Theta)).GetSafeNormal();
}

void ARPGSkyCycle::Apply()
{
	const FVector SunDir = GetSunDirection();
	const FVector MoonDir=GetMoonDirection();
	const FRPGWeatherState Weather=GetWeatherState();
	const float Illumination=GetMoonIllumination();
	const float Transmission=1.f-.78f*Weather.Clouds;
	const float Flash=WeatherEffects?WeatherEffects->GetLightningFlash():0;
	const float Height = SunDir.Z;
	const float Night = GetNightAmount();
	const float Low = 1.f - FMath::SmoothStep(0.04f, 0.42f, Height);        // 1 near the horizon (morning / sunset)
	if(!WaterLighting)WaterLighting=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/RPG/Environment/Weather/MPC_WeatherLighting.MPC_WeatherLighting"));
	if(WaterLighting)
	{
		const float Solar=FMath::SmoothStep(.0f,.35f,Height)*(1-.88f*Weather.Clouds)*(1-.4f*Weather.Haze);
		UKismetMaterialLibrary::SetScalarParameterValue(this,WaterLighting,TEXT("DirectSunlight"),Solar);
		const float Lunar=.04f*MoonLightScale*FMath::Pow(Illumination,1.5f)*FMath::SmoothStep(.0f,.35f,float(MoonDir.Z))*Transmission;
		UKismetMaterialLibrary::SetScalarParameterValue(this,WaterLighting,TEXT("WaterLight"),FMath::Clamp(Solar+Lunar,0.f,1.f));
		const float RainLight=FMath::SmoothStep(-.08f,.3f,Height)*(1-.45f*Weather.Clouds)+Lunar*3.f+Flash*2.f;
		UKismetMaterialLibrary::SetScalarParameterValue(this,WaterLighting,TEXT("RainLight"),FMath::Clamp(RainLight,.012f,1.5f));
	}
	if(CloudMaterial)
	{
        const float P=Weather.Puffs,B=Weather.Billows,K=Weather.Broken,L=Weather.Layers,S=Weather.Shelf;
        CloudMaterial->SetScalarParameterValue(TEXT("Layout_CloudGlobalScale"),P*40+B*90+K*24+L*60+S*20);
        const float Coverage=P*0.f+B*(-.05f)+K*.1f+L*1.5f+S*1.05f;
        CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"),FMath::Lerp(-.7f,Coverage,FMath::Clamp(Weather.Clouds/.35f,0.f,1.f)));
        CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"),P*.3f+B*.3f+K*.028f+L*.2f+S*.1f);
        CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"),0);
        CloudMaterial->SetVectorParameterValue(TEXT("Layout_CloudType"),FLinearColor(P+B+K*.8f+L*.25f+S*.3f,P*.04f+B*.02f+K*.35f+L*.8f+S*.25f,K*.1f+L*.05f,B*.18f+K*.1f+L+S*1.7f));
        CloudMaterial->SetVectorParameterValue(TEXT("Noise_Strength"),FLinearColor(.85f-P*.2f-B*.15f,.15f-P*.03f-B*.03f,.08f-P*.04f-B*.04f,2.2f-P*.6f-B*.5f));
        CloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"),FLinearColor::LerpUsingHSV(FLinearColor(.68f,.8f,1.f,.45f),FLinearColor(.55f,.59f,.65f,.7f),FMath::Clamp(L*.3f+S,0.f,1.f)));
        // Position is integrated in Tick; changing wind never jumps Time*velocity.
        CloudMaterial->SetVectorParameterValue(TEXT("Layout_WindControls"),FLinearColor(0,0,0,0));
        if(CloudVolume){CloudVolume->SetLayerBottomAltitude(P*1.1f+B*1.1f+K*1.1f+L*.9f+S*.7f);CloudVolume->SetLayerHeight(P*2+B*6+K*2.8f+L*2.2f+S*4.2f);}
        for(const auto& O:CloudScalarOverrides)
        {
            if(O.Key==TEXT("LayerBottomKm")){if(CloudVolume)CloudVolume->SetLayerBottomAltitude(O.Value);}
            else if(O.Key==TEXT("LayerHeightKm")){if(CloudVolume)CloudVolume->SetLayerHeight(O.Value);}
            else if(O.Key==TEXT("ViewSampleScale")){if(CloudVolume)CloudVolume->SetViewSampleCountScale(O.Value);}
            else if(O.Key==TEXT("SkyBottomOcclusion")){if(CloudVolume)CloudVolume->SetSkyLightCloudBottomOcclusion(O.Value);}
            else if(O.Key==TEXT("SunCloudScatter"))
            {
                if(Sun)if(auto* Light=Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
                {Light->CloudScatteredLuminanceScale=FLinearColor(O.Value,O.Value,O.Value);Light->MarkRenderStateDirty();}
            }
            else CloudMaterial->SetScalarParameterValue(O.Key,O.Value);
        }
        for(const auto& O:CloudVectorOverrides)CloudMaterial->SetVectorParameterValue(O.Key,O.Value);
        for(const auto& O:CloudTextureOverrides)
            if(UTexture* Tex=LoadObject<UTexture>(nullptr,*O.Value))CloudMaterial->SetTextureParameterValue(O.Key,Tex);
            else UE_LOG(LogTemp,Warning,TEXT("CloudLook texture override not found: %s"),*O.Value);
	}

	if (Sun)
	{
		// Light travels away from the sun. The atmosphere reddens and finally blocks it near/below the horizon.
		Sun->SetActorRotation((-SunDir).Rotation());
		if (ULightComponent* L = Sun->GetLightComponent())
		{
			L->SetIntensity(SunIntensity * FMath::SmoothStep(-0.1f, 0.02f, Height) * Transmission);
			L->SetLightColor(FLinearColor::LerpUsingHSV(SunColorNoon, SunColorLow, Low));
		}
	}
	if (Moon)
	{
		Moon->SetActorRotation((-MoonDir).Rotation());
		if (ULightComponent* L = Moon->GetLightComponent())
		{
			L->SetIntensity(MoonIntensity * MoonLightScale * Night * FMath::SmoothStep(-0.05f, 0.08f, float(MoonDir.Z)) * FMath::Pow(Illumination,1.5f) * Transmission * (1-.4f*Weather.Haze));
			L->SetLightColor(FLinearColor::LerpUsingHSV(MoonColor,FLinearColor(.85f,.75f,.59f),Weather.Haze*.4f));
		}
	}
	if (SkyLight && SkyLight->GetLightComponent())
		SkyLight->GetLightComponent()->SetIntensity(FMath::Lerp(SkyLightDay, SkyLightNight*(.4f+.6f*Illumination*MoonLightScale), Night)*(1-.48f*Weather.Clouds));
	if (Fog && Fog->GetComponent())
	{
		const FLinearColor DayHaze = FMath::Lerp(HazeDay, HazeLow, Low);
		auto* FC=Fog->GetComponent();
		if(!bCachedFog){BaseFogDensity=FC->FogDensity;BaseFogFalloff=FC->FogHeightFalloff;BaseFogStart=FC->StartDistance;BaseFogMaxOpacity=FC->FogMaxOpacity;
			BaseVolumeDistance=FC->VolumetricFogDistance;BaseVolumeStart=FC->VolumetricFogStartDistance;BaseVolumeFade=FC->VolumetricFogNearFadeInDistance;bCachedFog=true;}
		const float FogBlend=FMath::Clamp(Weather.Fog/.85f,0.f,1.f);
		FC->SetFogDensity((BaseFogDensity+Weather.Haze*.012f+Weather.Rain*.008f)*(1-FogBlend));
		FC->SetFogMaxOpacity(FMath::Lerp(BaseFogMaxOpacity,.96f,FogBlend));
		FC->SetFogHeightFalloff(BaseFogFalloff);
		FC->SetStartDistance(FMath::Lerp(BaseFogStart,0.f,FogBlend));
		FC->SetVolumetricFogStartDistance(FMath::Lerp(BaseVolumeStart,0.f,FogBlend));
		FC->SetVolumetricFogNearFadeInDistance(FMath::Lerp(BaseVolumeFade,0.f,FogBlend));
		FC->SetVolumetricFogDistance(FMath::Lerp(BaseVolumeDistance,FMath::Max(BaseVolumeDistance,40000.f),FogBlend));
		Fog->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(DayHaze,HazeNight,Night),FLinearColor(.35f,.39f,.43f)*FMath::Lerp(1.f,.08f,Night),Weather.Clouds*.55f));
		Fog->GetComponent()->SetDirectionalInscatteringColor(FLinearColor(0.9f, 0.62f, 0.35f) * (1.f - Night) * (0.25f + 0.75f * Low));
	}
	if(GroundFog)GroundFog->SetWeather(Weather.Fog);
	if (Grade)
	{
		Grade->Settings.bOverride_AutoExposureBias = true;
		Grade->Settings.AutoExposureBias = FMath::Lerp(ExposureBiasDay, ExposureBiasNight, Night)-Weather.Clouds*.25f-Weather.Storm*.65f;
	}
	const double ElapsedDays=CalendarSeconds()/86400.0;
	FRandomStream ShadeA(int32(FMath::FloorToDouble(ElapsedDays))+9173),ShadeB(int32(FMath::FloorToDouble(ElapsedDays))+9174);
	const float DailyShade=FMath::Lerp(ShadeA.FRandRange(0.f,.06f),ShadeB.FRandRange(0.f,.06f),float(FMath::Frac(ElapsedDays)));
	const float MoonWarm=FMath::Clamp(DailyShade+Weather.Haze*.65f+(1-FMath::SmoothStep(.02f,.45f,float(MoonDir.Z)))*.3f,0.f,1.f);
	const float Data[17] = { float(SunDir.X), float(SunDir.Y), float(SunDir.Z), float(MoonDir.X), float(MoonDir.Y), float(MoonDir.Z), Night, Height,
		GetMoonPhase(),Weather.Clouds,Weather.Haze,Weather.Fog,MoonWarm,Weather.Wind,Weather.Storm,Flash,CloudMaterial?1.f:0.f };
	const bool bGame = GetWorld() && GetWorld()->IsGameWorld();
	for (AActor* Receiver : SkyReceivers)
	{
		if (!Receiver) continue;
		TInlineComponentArray<UPrimitiveComponent*> Prims(Receiver);
		for (UPrimitiveComponent* Prim : Prims)
			for (int32 i = 0; i < 17; ++i)
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
