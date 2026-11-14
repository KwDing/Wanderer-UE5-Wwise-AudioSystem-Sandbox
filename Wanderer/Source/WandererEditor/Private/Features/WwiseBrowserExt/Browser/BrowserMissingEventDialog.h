#pragma once

#include "CoreMinimal.h"

struct FAssetData;

class FBrowserMissingEventDialog
{
public:
    static bool SelectMissingEvents(TArray<FAssetData>& InOutObjects);
};
