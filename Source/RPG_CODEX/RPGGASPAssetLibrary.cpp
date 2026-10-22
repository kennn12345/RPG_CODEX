#include "RPGGASPAssetLibrary.h"
#include "Animation/BlendSpace.h"

bool URPGGASPAssetLibrary::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!IsValid(BlendSpace) || !BlendSpace->GetSkeleton()) return false;
    BlendSpace->Modify();
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
