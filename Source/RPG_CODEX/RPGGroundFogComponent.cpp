#include "RPGGroundFogComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "RPGWaterZoneComponent.h"

URPGGroundFogComponent::URPGGroundFogComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void URPGGroundFogComponent::BeginPlay()
{
    Super::BeginPlay();
    // Trace actual terrain components, not tree canopies or roofs.
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TArray<UPrimitiveComponent*> Parts;It->GetComponents(Parts);
        for(auto* Part:Parts)
        {
            if(auto* Zone=Cast<URPGWaterZoneComponent>(Part))Water.Add(Zone);
            bool Accept=Part->GetClass()->GetName().Contains(TEXT("LandscapeHeightfieldCollision"));
            if(auto* Mesh=Cast<UStaticMeshComponent>(Part))if(UStaticMesh* Asset=Mesh->GetStaticMesh())
            {
                const FString Name=Asset->GetName();
                Accept|=Name.Contains(TEXT("Terrain")) || Name==TEXT("SM_Vista_Far");
            }
            if(Accept)Ground.Add(Part);
        }
    }
}
float URPGGroundFogComponent::GroundHeight(float X,float Y)const
{
    float Height=-BIG_NUMBER;FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundFogTerrain),true);
    for(const auto& Weak:Ground)if(auto* Part=Weak.Get())
    {
        FHitResult Hit;
        if(Part->LineTraceComponent(Hit,FVector(X,Y,1000000),FVector(X,Y,-1000000),Query))Height=FMath::Max(Height,float(Hit.ImpactPoint.Z));
    }
    for(const auto& Weak:Water)if(auto* Zone=Weak.Get())if(Zone->ContainsXY(FVector(X,Y,0)))
        Height=FMath::Max(Height,float(Zone->GetComponentLocation().Z+Zone->GetScaledBoxExtent().Z));
    return Height;
}
void URPGGroundFogComponent::SetWeather(float Fog)
{
    Strength=FMath::Clamp(Fog/.85f,0.f,1.f);
    if(HasBegunPlay() && Strength>.001f && Volumes.IsEmpty())UpdateGrid();
}
void URPGGroundFogComponent::UpdateGrid()
{
    if(Strength<=.001f || Ground.IsEmpty())return;
    FVector Camera;FRotator View;
    auto* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;PC->GetPlayerViewPoint(Camera,View);
    constexpr float Spacing=2500,Radius=4000,Outer=19000;
    const FIntPoint Cell(FMath::RoundToInt(Camera.X/Spacing),FMath::RoundToInt(Camera.Y/Spacing));
    if(Cell==Anchor && !Volumes.IsEmpty())return;Anchor=Cell;
    if(HeightCache.Num()>4096)HeightCache.Reset();
    for(int32 Y=-8;Y<=8;++Y)for(int32 X=-8;X<=8;++X)
    {
        const FIntPoint Key=Cell+FIntPoint(X,Y);const float WX=Key.X*Spacing,WY=Key.Y*Spacing;
        if(Cells.Contains(Key) || FVector2D(WX-Camera.X,WY-Camera.Y).Size()>Outer)continue;
        float Lowest=BIG_NUMBER;
        for(int32 DY=-2;DY<=2;DY+=2)for(int32 DX=-2;DX<=2;DX+=2)
        {
            const FIntPoint Sample=Key+FIntPoint(DX,DY);float* Height=HeightCache.Find(Sample);
            if(!Height){HeightCache.Add(Sample,GroundHeight(Sample.X*Spacing,Sample.Y*Spacing));Height=HeightCache.Find(Sample);}
            if(*Height>-BIG_NUMBER*.5f)Lowest=FMath::Min(Lowest,*Height);
        }
        if(Lowest>=BIG_NUMBER*.5f)continue;
        auto* Volume=NewObject<ULocalFogVolumeComponent>(GetOwner());GetOwner()->AddInstanceComponent(Volume);
        Volume->SetMobility(EComponentMobility::Movable);
        Volume->SetRadialFogExtinction(0);Volume->SetHeightFogExtinction(0);
        Volume->SetHeightFogFalloff(1.f);Volume->SetHeightFogOffset(.8f);
        Volume->SetFogAlbedo(FLinearColor(.85f,.88f,.9f));Volume->SetFogEmissive(FLinearColor::Black);
        Volume->SetFogPhaseG(.1f);Volume->SetFogStartDistance(0);
        Volume->SetWorldScale3D(FVector(Radius/ULocalFogVolumeComponent::GetBaseVolumeSize()));
        // Stable world cells are never moved when the camera crosses a grid boundary.
        Volume->SetWorldLocation(FVector(WX,WY,Lowest-Radius+LayerHeight));
        Volume->SetVisibility(false);Volume->RegisterComponent();Volumes.Add(Volume);
        FCell Entry;Entry.Volume=Volume;Cells.Add(Key,Entry);
    }
}
void URPGGroundFogComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);Timer+=Delta;
    if(Timer>.2f){Timer=0;UpdateGrid();}
    FVector Camera;FRotator View;auto* PC=GetWorld()->GetFirstPlayerController();if(!PC)return;PC->GetPlayerViewPoint(Camera,View);
    for(auto It=Cells.CreateIterator();It;++It)
    {
        auto& Cell=It.Value();auto* Volume=Cell.Volume;
        const FVector Location=Volume->GetComponentLocation();
        const float Distance=FVector2D(Location.X-Camera.X,Location.Y-Camera.Y).Size();
        const float Target=Strength*(1-FMath::SmoothStep(11000.f,18000.f,Distance));
        // Rate-limited density changes avoid popping even during rapid flight or weather switches.
        Cell.Alpha=FMath::FInterpConstantTo(Cell.Alpha,Target,Delta,.4f);
        Volume->SetRadialFogExtinction(Cell.Alpha*6.f);Volume->SetHeightFogExtinction(Cell.Alpha*.35f);
        Volume->SetVisibility(Cell.Alpha>.001f);
        if(Cell.Alpha<=.001f && (Distance>19000 || Strength<=.001f))
        {
            Volumes.Remove(Volume);Volume->DestroyComponent();It.RemoveCurrent();
        }
    }
}
