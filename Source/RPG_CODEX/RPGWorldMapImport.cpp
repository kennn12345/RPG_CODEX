// Editor-only, explicitly invoked terrain import. Never runs during normal play.
#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"

static void ImportWorldMapBlockout()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->GetOutermost()->GetName() != TEXT("/Game/RPG/Maps/L_WorldMap_Blockout"))
    {
        UE_LOG(LogTemp, Error, TEXT("WORLD_MAP_IMPORT refused: expected dedicated blockout map"));
        return;
    }
    TArray<uint8> Raw;
    const FString Path = FPaths::ProjectDir() / TEXT("Tools/World/WorldMapSource/heightmap.r16");
    if (!FFileHelper::LoadFileToArray(Raw, *Path) || Raw.Num() != 4033 * 2521 * 2)
    {
        UE_LOG(LogTemp, Error, TEXT("WORLD_MAP_IMPORT invalid height data"));
        return;
    }
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        UE_LOG(LogTemp, Error, TEXT("WORLD_MAP_IMPORT refused: landscape already exists"));
        return;
    }
    ALandscape* Land = World->SpawnActor<ALandscape>();
    Land->SetActorLabel(TEXT("WorldMap_Terrain_14km_x_9km"));
    Land->SetActorLocation(FVector(-700000.0, -450000.0, 0.0));
    Land->SetActorScale3D(FVector(347.22222222222223, 357.14285714285717, 100.0));
    Land->LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RPG/Environment/WorldMap/M_WorldMap_Blockout.M_WorldMap_Blockout"));
    Land->StaticLightingLOD = 3;
    TMap<FGuid, TArray<uint16>> Heights;
    TArray<uint16>& Samples = Heights.Add(FGuid());
    Samples.SetNumUninitialized(4033 * 2521);
    FMemory::Memcpy(Samples.GetData(), Raw.GetData(), Raw.Num());
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> Layers;
    Layers.Add(FGuid());
    Land->Import(FGuid::NewGuid(), 0, 0, 4032, 2520, 2, 63, Heights, *Path,
        Layers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
    Land->GetLandscapeInfo()->UpdateLayerInfoMap(Land);
    Land->PostEditChange();
    Land->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("WORLD_MAP_IMPORT complete components=%d"), Land->LandscapeComponents.Num());
}

static FAutoConsoleCommand WorldMapImportCommand(
    TEXT("RPG.Editor.ImportWorldMapBlockout"), TEXT("Import the isolated 14x9 km terrain blockout"),
    FConsoleCommandDelegate::CreateStatic(&ImportWorldMapBlockout));

static void VerifyWorldMapBlockout()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->GetOutermost()->GetName() != TEXT("/Game/RPG/Maps/L_WorldMap_Blockout")) return;
    TArray<uint8> Raw;
    if (!FFileHelper::LoadFileToArray(Raw, *(FPaths::ProjectDir() / TEXT("Tools/World/WorldMapSource/heightmap.r16")))
        || Raw.Num() != 4033 * 2521 * 2) return;
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        ALandscape* Land = *It;
        TArray<uint16> Actual;
        Actual.SetNumZeroed(4033 * 2521);
        FLandscapeEditDataInterface Edit(Land->GetLandscapeInfo());
        Edit.GetHeightDataFast(0, 0, 4032, 2520, Actual.GetData(), 4033);
        int32 Mismatches = 0, MaxError = 0;
        const uint16* Expected = reinterpret_cast<const uint16*>(Raw.GetData());
        for (int32 Index = 0; Index < Actual.Num(); ++Index)
        {
            const int32 Error = FMath::Abs(int32(Actual[Index]) - int32(Expected[Index]));
            if (Error) ++Mismatches;
            MaxError = FMath::Max(MaxError, Error);
        }
        const FString Report = FString::Printf(TEXT("{\"samples\":%d,\"mismatches\":%d,\"max_error_uint16\":%d,\"components\":%d}"),
            Actual.Num(), Mismatches, MaxError, Land->LandscapeComponents.Num());
        FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("WorldMapBlockout/height_verification.json")));
        UE_LOG(LogTemp, Display, TEXT("WORLD_MAP_VERIFY %s"), *Report);
    }
}

static FAutoConsoleCommand WorldMapVerifyCommand(
    TEXT("RPG.Editor.VerifyWorldMapBlockout"), TEXT("Compare every saved terrain height with the source"),
    FConsoleCommandDelegate::CreateStatic(&VerifyWorldMapBlockout));
#endif
