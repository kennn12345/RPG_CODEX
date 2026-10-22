#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGGASPAssetLibrary.generated.h"

class UBlendSpace;

/** Editor support for GASP assets created by project setup scripts. */
UCLASS()
class RPG_CODEX_API URPGGASPAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Rebuild runtime interpolation data after editing a blend space's samples through Python. */
    UFUNCTION(BlueprintCallable, Category = "RPG|GASP|Editor")
    static bool RebuildBlendSpace(UBlendSpace* BlendSpace);
};
