#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/SoftObjectPath.h"

class IBrowserInteraction;
struct FAssetData;
struct FBrowserCatalogRecord;
struct FWwiseTreeItem;

/** View-owned workflow; observes presentation weakly, never includes concrete UI. */
class FBrowserSyncController : public TSharedFromThis<FBrowserSyncController>
{
public:
    explicit FBrowserSyncController(const TSharedRef<IBrowserInteraction>& InInteraction);
    static TArray<FAssetData> FindUnused(
        const FString& InRoot, const TSet<FGuid>& InLiveGUIDs, FString& OutSkipped);
    static bool RemoveUnused(const TArray<FAssetData>& InAssets,
        FString& OutError,
        TFunction<bool()> InCanContinue = {});

    // Browser delegate entry points.
    void OnCleanFolders(TArray<FString> InPaths);
    void OnCleanSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    void OnDeleteMissingEvents(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    bool OnOverrideImport(const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);
    bool OnPrepareDrag(
        const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection, TArray<FAssetData>& OutAssets);
    void OnRememberSource(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    void OnReplaceWithSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    void OnSyncSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    void OnToggleSync();

    bool IsSyncEnabled() const;

private:
    TWeakPtr<IBrowserInteraction> InteractionWeakPtr;
    uint64 InteractionGeneration = 0;
    TArray<FSoftObjectPath> ReplacementSourceSoftRefs;
    bool bSyncEnabled = true;

    // Asset workflows.
    void CleanFolders(TArray<FString> InPaths);
    void DeleteMissingEvents(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);
    void SyncSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection);

    void CollectReplacementSourceSoftRefs(
        const FBrowserCatalogRecord& InRecord, TArray<FSoftObjectPath>& InOutSourceSoftRefs);
    bool ConfirmReplacement(
        const FBrowserCatalogRecord& InRecord, const TArray<FSoftObjectPath>& InSourceSoftRefs);
    // Shared workflow checks and presentation.
    bool CanContinue() const;
    bool Confirm(const FString& InText);
    static void RefreshBrowser();
    void ShowResult(const FString& InText);

};
