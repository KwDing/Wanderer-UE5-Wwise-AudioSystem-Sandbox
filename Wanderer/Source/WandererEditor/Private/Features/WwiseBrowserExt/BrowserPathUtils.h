#pragma once

#include "CoreMinimal.h"

namespace WwiseBrowserPaths
{
    FString NormalizePath(FString InPath);
    bool IsWithin(const FString& InPath, const FString& InRoot);
} // namespace WwiseBrowserPaths
