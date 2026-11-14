#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"

#include "Misc/Paths.h"
#include "PackageTools.h"

#include "AkAudioEvent.h"
#include "Wwise/WwiseTreeItem.h"
#include "WwiseUnrealHelper.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/Browser/BrowserInteraction.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"
#include "Features/WwiseBrowserExt/Catalog/WwiseEventHierarchyParser.h"

namespace
{
    void FindMissingEvents(const TMap<FGuid, FString>& InPaths,
        const TArray<TSharedPtr<FWwiseTreeItem>>& InRoots,
        TArray<FAssetData>& OutMissing,
        FString& OutSkipped)
    {
        for (const FAssetData& Asset :
            FWwiseAssetRepository::FindAudioAssets(FWwiseAssetRepository::Root))
        {
            const FGuid GUID = FWwiseAssetRepository::FindAssetGUID(Asset);
            if (Asset.AssetClassPath != UAkAudioEvent::StaticClass()->GetClassPathName()
                || !GUID.IsValid() || InPaths.Contains(GUID))
            {
                continue;
            }
            const bool bSelected = InRoots.ContainsByPredicate(
                [&Asset, GUID](const TSharedPtr<FWwiseTreeItem>& InItem)
                {
                    return InItem->ItemId == GUID
                           || (InItem->IsFolder()
                               && WwiseBrowserPaths::IsWithin(Asset.PackageName.ToString(),
                                   UPackageTools::SanitizePackageName(
                                       FWwiseAssetRepository::Root
                                       + WwiseBrowserPaths::NormalizePath(InItem->FolderPath))));
                });
            if (!bSelected)
            {
                continue;
            }
            if (FWwiseAssetRepository::HasPackageReferencers(Asset))
            {
                OutSkipped += TEXT("仍被引用，保留：") + Asset.GetObjectPathString() + TEXT("\n");
                continue;
            }
            OutMissing.Add(Asset);
        }
    }

    bool ValidateMissingEventSources(
        const FString& InDirectory, const TArray<FAssetData>& InAssets, FString& OutError)
    {
        TMap<FGuid, FString> Current;
        if (!FWwiseEventHierarchyParser::Parse(InDirectory, Current, OutError))
        {
            return false;
        }
        for (const FAssetData& Asset : InAssets)
        {
            if (Current.Contains(FWwiseAssetRepository::FindAssetGUID(Asset)))
            {
                OutError = TEXT("Wwise 工程已变化，取消删除，请重试。");
                return false;
            }
        }

        return true;
    }

} // namespace

void FBrowserSyncController::OnDeleteMissingEvents(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    DeleteMissingEvents(MoveTemp(InSelection));
}

void FBrowserSyncController::DeleteMissingEvents(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate())
    {
        ShowResult(TEXT("请停止 PIE。"));
        return;
    }
    const TArray<TSharedPtr<FWwiseTreeItem>> Roots =
        FBrowserCatalogProvider::ReduceSelection(InSelection);
    if (Roots.IsEmpty())
    {
        ShowResult(TEXT("请先选择需要核对的 Events 目录或对象。"));
        return;
    }
    TMap<FGuid, FString> Paths;
    FString Error;
    const FString Directory =
        FPaths::GetPath(WwiseUnrealHelper::GetWwiseProjectPath()) / TEXT("Events");
    if (!FWwiseEventHierarchyParser::Parse(Directory, Paths, Error))
    {
        ShowResult(Error);
        return;
    }
    TArray<FAssetData> Missing;
    FString Skipped;
    FindMissingEvents(Paths, Roots, Missing, Skipped);
    if (Missing.IsEmpty())
    {
        ShowResult(TEXT("所选范围没有可清理的缺失 Event。\n") + Skipped);
        return;
    }
    const TSharedPtr<IBrowserInteraction> Interaction = InteractionWeakPtr.Pin();
    if (!Interaction || !Interaction->SelectMissingEvents(Missing) || !CanContinue()
        || !FWwiseAssetSyncOperation::CanMutate())
    {
        return;
    }
    if (!ValidateMissingEventSources(Directory, Missing, Error))
    {
        ShowResult(Error);
        return;
    }
    // Backup may enter a save dialog. Check the saved source again before UE deletes anything.
    RemoveUnused(Missing,
        Error,
        [this, &Directory, &Missing, &Error]
        {
            return CanContinue() && ValidateMissingEventSources(Directory, Missing, Error);
        });
    RefreshBrowser();
    ShowResult(Error + TEXT("\n") + Skipped);
}
