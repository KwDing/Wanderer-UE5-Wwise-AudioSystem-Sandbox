#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FAssetData;
struct FBrowserCatalogRecord;
struct FSoftObjectPath;

class FWwiseAssetSyncOperation
{
public:
    // Synchronous asset operations.
    static bool CanMutate();
    static bool Consolidate(const FSoftObjectPath& InOldSoftRef,
        const FSoftObjectPath& InNewSoftRef,
        FString& OutError,
        const FGuid& InExpectedSharedGUID = FGuid());
    static bool PrepareDragAssets(const TArray<FBrowserCatalogRecord>& InRecords,
        const FString& InAssetRoot,
        TArray<FAssetData>& OutAssets,
        FString& OutError,
        TFunctionRef<bool()> InCanContinue,
        const FString& InSearchRoot = TEXT("/Game"));
    static bool SyncRecord(const FBrowserCatalogRecord& InRecord,
        const FString& InAssetRoot,
        FString& OutResult,
        const FString& InSearchRoot = TEXT("/Game"));
};
