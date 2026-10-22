#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGGASPAssetLibrary.generated.h"

class UBlendSpace;
class UAnimBlueprint;
class UAnimSequence;
class URPGPrototypeAnimationComponent;

/** Editor support for GASP assets created by project setup scripts. */
UCLASS()
class RPG_CODEX_API URPGGASPAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Rebuild runtime interpolation data after editing a blend space's samples through Python. */
    UFUNCTION(BlueprintCallable, Category = "RPG|GASP|Editor")
    static bool RebuildBlendSpace(UBlendSpace* BlendSpace);

    /** PIE-only in-place guard fixture. Does not edit or save any animation asset. */
    UFUNCTION(BlueprintCallable, Category = "RPG|GASP|Editor")
    static UAnimSequence* CreateGuardTestFixture(URPGPrototypeAnimationComponent* Component, UAnimSequence* Sequence);

    /** Configure the imported CMC AnimBP or a project-owned copy; the caller saves after checking the result. */
    UFUNCTION(BlueprintCallable, Category = "RPG|GASP|Editor")
    static FString ConfigurePlayerAnimBlueprint(UAnimBlueprint* Blueprint);
};


