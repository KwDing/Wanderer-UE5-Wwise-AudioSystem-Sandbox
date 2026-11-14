#pragma once

#include "CoreMinimal.h"

struct FBrowserCatalogRecord;
struct FWwiseTreeItem;

class FBrowserCatalogProvider
{
public:
    // Selection boundaries.
    static bool IsSelected(const FBrowserCatalogRecord& InRecord,
        const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);
    static TArray<TSharedPtr<FWwiseTreeItem>> ReduceSelection(
        const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);
    // Catalog snapshots.
    static bool IsCatalogCurrent(const TArray<FBrowserCatalogRecord>& InRecords);
    static bool ReadCatalog(TArray<FBrowserCatalogRecord>& OutRecords,
        FString& OutError,
        const TArray<TSharedPtr<FWwiseTreeItem>>* InSelection = nullptr);
    static bool ReadSelectedCatalog(const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection,
        TArray<FBrowserCatalogRecord>& OutRecords,
        FString& OutError);

};
