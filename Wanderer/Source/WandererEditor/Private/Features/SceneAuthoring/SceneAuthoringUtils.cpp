#include "Features/SceneAuthoring/SceneAuthoringUtils.h"

bool SceneAuthoringUtils::IsAllowedAsset(
    const UObject* InAsset, TConstArrayView<const TCHAR*> InPaths)
{
    if (!IsValid(InAsset))
    {
        return false;
    }
    const FString Path = InAsset->GetPathName();
    return InPaths.ContainsByPredicate(
        [&Path](const TCHAR* InCandidate)
        {
            return Path == InCandidate;
        });
}
