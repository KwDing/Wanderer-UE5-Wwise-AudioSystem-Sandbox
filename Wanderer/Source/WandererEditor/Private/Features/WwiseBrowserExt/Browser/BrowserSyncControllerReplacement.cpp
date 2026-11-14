#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "Wwise/WwiseTreeItem.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"

void FBrowserSyncController::OnRememberSource(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    if (!CanContinue())
    {
        return;
    }
    ReplacementSourceSoftRefs.Reset();
    const TArray<FAssetData> Assets =
        FWwiseAssetRepository::FindAudioAssets(FWwiseAssetRepository::Root);
    for (const TSharedPtr<FWwiseTreeItem>& Item : InSelection)
    {
        if (!Item || Item->IsFolder())
        {
            continue;
        }
        {
            for (const FAssetData& Asset : Assets)
            {
                if (FWwiseAssetRepository::FindAssetGUID(Asset) == Item->ItemId)
                {
                    ReplacementSourceSoftRefs.AddUnique(Asset.GetSoftObjectPath());
                }
            }
        }
    }
    ShowResult(
        FString::Printf(TEXT("已记录 %d 个旧 UE 资产。接着右键新 Wwise 对象，选择替换引用。"),
            ReplacementSourceSoftRefs.Num()));
}

void FBrowserSyncController::OnReplaceWithSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    TArray<FBrowserCatalogRecord> Catalog;
    FString Error;
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::ReadSelectedCatalog(InSelection, Catalog, Error))
    {
        ShowResult(Error.IsEmpty() ? TEXT("请停止 PIE。") : Error);
        return;
    }
    if (Catalog.Num() != 1)
    {
        ShowResult(TEXT("请只选一个新的、已生成的 Wwise 对象。"));
        return;
    }
    TArray<FSoftObjectPath> SourceSoftRefs = ReplacementSourceSoftRefs;
    if (SourceSoftRefs.IsEmpty())
    {
        CollectReplacementSourceSoftRefs(Catalog[0], SourceSoftRefs);
    }
    if (SourceSoftRefs.IsEmpty())
    {
        ShowResult(TEXT("先记录旧 Wwise 对象，或在 Content Browser 中选中旧资产。"));
        return;
    }
    if (!ConfirmReplacement(Catalog[0], SourceSoftRefs))
    {
        return;
    }
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::IsCatalogCurrent(Catalog))
    {
        ShowResult(TEXT("生成数据在预览期间发生变化，请刷新后重试。"));
        return;
    }
    if (!FWwiseAssetSyncOperation::SyncRecord(Catalog[0], FWwiseAssetRepository::Root, Error))
    {
        ShowResult(Error);
        return;
    }
    FSoftObjectPath TargetSoftRef;
    for (const FAssetData& Asset :
        FWwiseAssetRepository::FindAudioAssets(FWwiseAssetRepository::Root))
    {
        if (FWwiseAssetRepository::FindAssetGUID(Asset) == Catalog[0].GUID
            && Asset.AssetClassPath == Catalog[0].ClassPath)
        {
            TargetSoftRef = Asset.GetSoftObjectPath();
        }
    }
    if (TargetSoftRef.IsNull())
    {
        ShowResult(TEXT("新资产创建后未能找到，请刷新。"));
        return;
    }
    for (const FSoftObjectPath& SourceSoftRef : SourceSoftRefs)
    {
        if (!CanContinue())
        {
            return;
        }
        if (SourceSoftRef == TargetSoftRef || SourceSoftRef.TryLoad() == TargetSoftRef.TryLoad())
        {
            continue;
        }
        if (!FWwiseAssetSyncOperation::Consolidate(SourceSoftRef, TargetSoftRef, Error))
        {
            ShowResult(Error);
            return;
        }
    }
    ReplacementSourceSoftRefs.Reset();
    RefreshBrowser();
    ShowResult(TEXT("引用替换完成；旧路径保留重定向器。请保存、检查引用并测试播放。"));
}

void FBrowserSyncController::CollectReplacementSourceSoftRefs(
    const FBrowserCatalogRecord& InRecord, TArray<FSoftObjectPath>& InOutSourceSoftRefs)
{
    TArray<FAssetData> Selected;
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
        .Get()
        .GetSelectedAssets(Selected);
    for (const FAssetData& Asset : Selected)
    {
        if (WwiseBrowserPaths::IsWithin(Asset.PackageName.ToString(), FWwiseAssetRepository::Root)
            && Asset.AssetClassPath == InRecord.ClassPath)
        {
            InOutSourceSoftRefs.AddUnique(Asset.GetSoftObjectPath());
        }
    }
}

bool FBrowserSyncController::ConfirmReplacement(
    const FBrowserCatalogRecord& InRecord, const TArray<FSoftObjectPath>& InSourceSoftRefs)
{
    FString Preview = TEXT("将以下旧资产的 UE 引用替换到：\n") + InRecord.Package + TEXT("\n\n");
    for (const FSoftObjectPath& SourceSoftRef : InSourceSoftRefs)
    {
        const FAssetData Asset =
            FWwiseAssetRepository::GetRegistry().GetAssetByObjectPath(SourceSoftRef);
        if (Asset.AssetClassPath != InRecord.ClassPath)
        {
            ShowResult(TEXT("旧/新资产类型不同，不能替换。"));
            return false;
        }
        Preview += SourceSoftRef.ToString() + TEXT("\n");
    }
    if (!Confirm(Preview))
    {
        return false;
    }
    return true;
}
