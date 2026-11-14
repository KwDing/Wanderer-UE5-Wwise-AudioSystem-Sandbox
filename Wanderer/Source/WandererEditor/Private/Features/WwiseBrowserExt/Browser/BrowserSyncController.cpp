#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/ConfigCacheIni.h"

#include "IAudiokineticTools.h"
#include "Wwise/WwiseTreeItem.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/Browser/BrowserInteraction.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"

FBrowserSyncController::FBrowserSyncController(const TSharedRef<IBrowserInteraction>& InInteraction)
    : InteractionWeakPtr(InInteraction), InteractionGeneration(InInteraction->GetGeneration())
{
    GConfig->GetBool(
        TEXT("Wanderer.WwiseBrowser"), TEXT("SyncEnabled"), bSyncEnabled, GEditorPerProjectIni);
}

bool FBrowserSyncController::OnOverrideImport(const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    if (!bSyncEnabled)
    {
        return false;
    }
    SyncSelection(InSelection);
    return true;
}

bool FBrowserSyncController::OnPrepareDrag(
    const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection, TArray<FAssetData>& OutAssets)
{
    if (!bSyncEnabled)
    {
        return false;
    }
    OutAssets.Reset();
    TArray<FBrowserCatalogRecord> Catalog;
    FString Error;
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::ReadSelectedCatalog(InSelection, Catalog, Error))
    {
        ShowResult(Error.IsEmpty() ? TEXT("请停止 PIE。") : Error);
        return true;
    }
    Catalog.RemoveAll(
        [](const FBrowserCatalogRecord& InRecord)
        {
            return FWwiseAssetRepository::IsRetired(InRecord.GUID);
        });
    if (!FWwiseAssetSyncOperation::PrepareDragAssets(
            Catalog,
            FWwiseAssetRepository::Root,
            OutAssets,
            Error,
            [this]
            {
                return CanContinue();
            }))
    {
        ShowResult(Error);
    }
    return true;
}

void FBrowserSyncController::OnSyncSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    SyncSelection(MoveTemp(InSelection));
}

void FBrowserSyncController::OnToggleSync()
{
    if (!CanContinue())
    {
        return;
    }
    bSyncEnabled = !bSyncEnabled;
    GConfig->SetBool(
        TEXT("Wanderer.WwiseBrowser"), TEXT("SyncEnabled"), bSyncEnabled, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

bool FBrowserSyncController::IsSyncEnabled() const
{
    return bSyncEnabled;
}

void FBrowserSyncController::SyncSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    TArray<FBrowserCatalogRecord> Catalog;
    FString Error;
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::ReadSelectedCatalog(InSelection, Catalog, Error))
    {
        ShowResult(Error.IsEmpty() ? TEXT("请停止 PIE。") : Error);
        return;
    }
    Catalog.RemoveAll(
        [](const FBrowserCatalogRecord& InRecord)
        {
            return FWwiseAssetRepository::IsRetired(InRecord.GUID);
        });
    TSet<FString> Destinations;
    FString Preview;
    for (const FBrowserCatalogRecord& Record : Catalog)
    {
        if (Destinations.Contains(Record.Package.ToLower()))
        {
            ShowResult(TEXT("不同源对象映射到同一目标路径，取消：") + Record.Package);
            return;
        }
        Destinations.Add(Record.Package.ToLower());
        Preview += Record.Package + TEXT("\n");
    }
    if (!Confirm(
            TEXT(
                "按已保存源路径同步以下资产。同 GUID 的 /Game 资产会迁移；同名同类型可能替换绑定/引用。Event 播放前仍需生成 Banks。\n\n")
            + Preview))
    {
        return;
    }
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::IsCatalogCurrent(Catalog))
    {
        ShowResult(TEXT("生成数据在预览期间发生变化，请刷新后重试。"));
        return;
    }
    FString Results;
    bool bAllSucceeded = true;
    for (const FBrowserCatalogRecord& Record : Catalog)
    {
        if (!CanContinue())
        {
            return;
        }
        FString Result;
        const bool bSuccess =
            FWwiseAssetSyncOperation::SyncRecord(Record, FWwiseAssetRepository::Root, Result);
        bAllSucceeded &= bSuccess;
        Results += (bSuccess ? TEXT("") : TEXT("失败：")) + Result + TEXT("\n");
    }
    RefreshBrowser();
    if (!Results.IsEmpty())
    {
        ShowResult(Results);
    }
    if (bAllSucceeded && CanContinue())
    {
        DeleteMissingEvents(InSelection);
    }
}

bool FBrowserSyncController::CanContinue() const
{
    const TSharedPtr<IBrowserInteraction> Interaction = InteractionWeakPtr.Pin();
    return Interaction && Interaction->IsAvailable()
           && Interaction->GetGeneration() == InteractionGeneration;
}

bool FBrowserSyncController::Confirm(const FString& InText)
{
    const TSharedPtr<IBrowserInteraction> Interaction = InteractionWeakPtr.Pin();
    return Interaction && Interaction->Confirm(InText);
}

void FBrowserSyncController::RefreshBrowser()
{
    if (IAudiokineticTools* Tools =
            FModuleManager::GetModulePtr<IAudiokineticTools>(TEXT("AudiokineticTools")))
    {
        Tools->RefreshWwiseProject();
    }
}

void FBrowserSyncController::ShowResult(const FString& InText)
{
    if (const TSharedPtr<IBrowserInteraction> Interaction = InteractionWeakPtr.Pin())
    {
        Interaction->ShowResult(InText);
    }
}
