#pragma once

#include "CoreMinimal.h"
#include "Wwise/Ref/WwiseAnyRef.h"

struct FBrowserCatalogRecord
{
    FGuid GUID;
    FString ObjectPath;
    FString Package;
    FTopLevelAssetPath ClassPath;
    WwiseAnyRef Ref;
    bool bSourceEvent = false;
};

struct FWwiseEventHierarchyNode
{
    FString Name;
    FGuid Parent;
    FString Root;
    bool bEvent = false;
};
