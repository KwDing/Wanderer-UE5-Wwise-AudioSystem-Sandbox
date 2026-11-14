#pragma once

#include "CoreMinimal.h"

namespace SceneAuthoringUtils
{
    bool IsAllowedAsset(const UObject* InAsset, TConstArrayView<const TCHAR*> InPaths);
}
