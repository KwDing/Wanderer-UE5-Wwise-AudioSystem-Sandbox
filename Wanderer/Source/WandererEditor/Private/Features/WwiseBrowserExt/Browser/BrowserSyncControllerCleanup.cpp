#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/Package.h"

#include "AkAudioType.h"
#include "AkInitBank.h"
#include "Wwise/WwiseTreeItem.h"
#include "WwiseUnrealHelper.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"

namespace
{
    bool LoadUnusedAssets(
        const TArray<FAssetData>& InAssets, TArray<UObject*>& OutObjects, FString& OutError)
    {
        OutObjects.Reset();
        for (const FAssetData& Asset : InAssets)
        {
            UAkAudioType* Object = Cast<UAkAudioType>(Asset.GetAsset());
            if (!IsValid(Object) || Object->IsA<UAkInitBank>()
                || FSoftObjectPath(Object) != Asset.GetSoftObjectPath()
                || Object->GetWwiseGuid() != FWwiseAssetRepository::FindAssetGUID(Asset)
                || FWwiseAssetRepository::HasPackageReferencers(Asset))
            {
                OutError = TEXT("对象或引用发生变化，取消删除：") + Asset.GetObjectPathString();
                return false;
            }
            OutObjects.Add(Object);
        }
        return true;
    }
} // namespace

TArray<FAssetData> FBrowserSyncController::FindUnused(
    const FString& InRoot, const TSet<FGuid>& InLiveGUIDs, FString& OutSkipped)
{
    TArray<FAssetData> Result;
    for (const FAssetData& Asset : FWwiseAssetRepository::FindAudioAssets(InRoot))
    {
        const FGuid GUID = FWwiseAssetRepository::FindAssetGUID(Asset);
        if (!GUID.IsValid()
            || Asset.AssetClassPath == UAkInitBank::StaticClass()->GetClassPathName()
            || InLiveGUIDs.Contains(GUID))
        {
            continue;
        }
        if (FWwiseAssetRepository::HasPackageReferencers(Asset))
        {
            OutSkipped += TEXT("仍被引用，保留：") + Asset.GetObjectPathString() + TEXT("\n");
            continue;
        }
        Result.Add(Asset);
    }
    return Result;
}

bool FBrowserSyncController::RemoveUnused(
    const TArray<FAssetData>& InAssets, FString& OutError, TFunction<bool()> InCanContinue)
{
    if (!FWwiseAssetSyncOperation::CanMutate())
    {
        OutError = TEXT("请停止 PIE。");
        return false;
    }
    TArray<UPackage*> Packages;
    TArray<UObject*> Objects;
    if (!LoadUnusedAssets(InAssets, Objects, OutError))
    {
        return false;
    }
    if (Objects.IsEmpty())
    {
        return true;
    }
    for (UObject* Object : Objects)
    {
        Packages.AddUnique(Object->GetOutermost());
    }
    if (!FWwiseAssetRepository::Backup(Packages, OutError))
    {
        return false;
    }
    if ((InCanContinue && !InCanContinue()) || !FWwiseAssetSyncOperation::CanMutate())
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("操作已停止，取消删除。");
        }
        return false;
    }
    if (!LoadUnusedAssets(InAssets, Objects, OutError))
    {
        return false;
    }
    // The feature already confirmed the request. The non-dialog path checks live references
    // without offering Force Delete.
    const int32 Deleted = ObjectTools::DeleteObjects(Objects, false);
    OutError = FString::Printf(
        TEXT("已删除 %d / %d 个缺失且无引用的 Wwise 资产。备份在 Saved/WwiseBrowserSync/Backups。"),
        Deleted,
        InAssets.Num());
    return Deleted == InAssets.Num();
}

void FBrowserSyncController::OnCleanFolders(TArray<FString> InPaths)
{
    CleanFolders(MoveTemp(InPaths));
}

namespace
{
    bool ValidateCleanupCatalog(const TArray<FBrowserCatalogRecord>& InCatalog,
        const TArray<FAssetData>& InCandidates,
        FString& InOutError)
    {
        for (const FBrowserCatalogRecord& Record : InCatalog)
        {
            if (InCandidates.ContainsByPredicate(
                    [Record](const FAssetData& InAsset)
                    {
                        return FWwiseAssetRepository::FindAssetGUID(InAsset) == Record.GUID;
                    }))
            {
                InOutError = TEXT("源数据在预览期间变化，取消删除，请刷新后重试。");
                return false;
            }
        }

        return true;
    }

    bool ValidateCleanupSourceTimes(
        const TMap<FString, FDateTime>& InSourceTimes, FString& InOutError)
    {
        for (const TPair<FString, FDateTime>& SourceTime : InSourceTimes)
        {
            if (IFileManager::Get().GetTimeStamp(*SourceTime.Key) != SourceTime.Value)
            {
                InOutError = TEXT("Wwise 源文件在预览期间变化，取消删除。");
                return false;
            }
        }
        TArray<FString> CurrentWorkUnits;
        IFileManager::Get().FindFilesRecursive(CurrentWorkUnits,
            *FPaths::GetPath(WwiseUnrealHelper::GetWwiseProjectPath()),
            TEXT("*.wwu"),
            true,
            false);
        if (CurrentWorkUnits.Num() != InSourceTimes.Num()
            || CurrentWorkUnits.ContainsByPredicate(
                [&InSourceTimes](const FString& InFile)
                {
                    return !InSourceTimes.Contains(InFile);
                }))
        {
            InOutError = TEXT("Wwise 源文件列表在预览期间变化，取消删除。");
            return false;
        }

        return true;
    }

    bool FilterAuthoringAssets(TArray<FAssetData>& InOutCandidates,
        TMap<FString, FDateTime>& InOutSourceTimes,
        FString& InOutSkipped,
        FString& OutError)
    {
        // A source object can be alive but excluded from generated Banks. Preserve such objects.
        TArray<FString> WorkUnits;
        IFileManager::Get().FindFilesRecursive(WorkUnits,
            *FPaths::GetPath(WwiseUnrealHelper::GetWwiseProjectPath()),
            TEXT("*.wwu"),
            true,
            false);
        TSet<FGuid> AuthoringGUIDs;
        for (const FString& File : WorkUnits)
        {
            InOutSourceTimes.Add(File, IFileManager::Get().GetTimeStamp(*File));
            FString Text;
            if (!FFileHelper::LoadFileToString(Text, *File))
            {
                OutError = TEXT("无法核对 Wwise 源文件，取消删除：") + File;
                return false;
            }
            for (const FAssetData& Asset : InOutCandidates)
            {
                if (Text.Contains(FWwiseAssetRepository::FindAssetGUID(Asset).ToString(
                        EGuidFormats::DigitsWithHyphensInBraces)))
                {
                    AuthoringGUIDs.Add(FWwiseAssetRepository::FindAssetGUID(Asset));
                }
            }
        }
        if (WorkUnits.IsEmpty())
        {
            OutError = TEXT("无法核对本地 Wwise 工程，取消删除。请先配置有效 Wwise 工程路径。");
            return false;
        }
        InOutCandidates.RemoveAll(
            [&AuthoringGUIDs, &InOutSkipped](const FAssetData& InAsset)
            {
                if (!AuthoringGUIDs.Contains(FWwiseAssetRepository::FindAssetGUID(InAsset)))
                {
                    return false;
                }
                InOutSkipped +=
                    TEXT("Wwise 工程仍存在，保留：") + InAsset.GetObjectPathString() + TEXT("\n");
                return true;
            });
        return true;
    }
} // namespace

void FBrowserSyncController::OnCleanSelection(TArray<TSharedPtr<FWwiseTreeItem>> InSelection)
{
    TArray<FString> InPaths;
    for (const TSharedPtr<FWwiseTreeItem>& Item :
        FBrowserCatalogProvider::ReduceSelection(InSelection))
    {
        if (Item->IsFolder())
        {
            InPaths.AddUnique(UPackageTools::SanitizePackageName(
                FWwiseAssetRepository::Root + WwiseBrowserPaths::NormalizePath(Item->FolderPath)));
        }
    }
    if (InPaths.IsEmpty())
    {
        ShowResult(TEXT("未选中有效目录，不执行清理。"));
        return;
    }
    CleanFolders(InPaths);
}

void FBrowserSyncController::CleanFolders(TArray<FString> InPaths)
{
    TArray<FBrowserCatalogRecord> Catalog;
    FString Error;
    if (!CanContinue() || !bSyncEnabled || !FWwiseAssetSyncOperation::CanMutate()
        || !FBrowserCatalogProvider::ReadCatalog(Catalog, Error))
    {
        ShowResult(Error.IsEmpty() ? TEXT("请开启 Sync 并停止 PIE。") : Error);
        return;
    }
    TSet<FGuid> LiveGUIDs;
    for (const FBrowserCatalogRecord& Record : Catalog)
    {
        LiveGUIDs.Add(Record.GUID);
    }
    TArray<FAssetData> Candidates;
    FString Skipped;
    for (const FString& Path : InPaths)
    {
        if (!WwiseBrowserPaths::IsWithin(Path, FWwiseAssetRepository::Root))
        {
            continue;
        }
        for (const FAssetData& Asset : FindUnused(Path, LiveGUIDs, Skipped))
        {
            Candidates.AddUnique(Asset);
        }
    }
    TMap<FString, FDateTime> SourceTimes;
    if (!FilterAuthoringAssets(Candidates, SourceTimes, Skipped, Error))
    {
        ShowResult(Error);
        return;
    }
    FString Preview;
    for (const FAssetData& Asset : Candidates)
    {
        Preview += Asset.GetObjectPathString() + TEXT("\n");
    }
    if (Candidates.IsEmpty())
    {
        ShowResult(TEXT("没有可删除的缺失且无引用资产。\n") + Skipped);
        return;
    }
    if (!Confirm(
            TEXT(
                "删除下列资产？它们在生成数据和本地已保存的 Wwise 工程中均未找到，并且没有 UE 包引用。请先保存 Wwise 并重新生成 Banks。\n\n")
            + Preview + TEXT("\n") + Skipped))
    {
        return;
    }
    if (!FBrowserCatalogProvider::ReadCatalog(Catalog, Error))
    {
        ShowResult(Error);
        return;
    }
    if (!CanContinue() || !FWwiseAssetSyncOperation::CanMutate()
        || !ValidateCleanupCatalog(Catalog, Candidates, Error)
        || !ValidateCleanupSourceTimes(SourceTimes, Error))
    {
        ShowResult(Error);
        return;
    }
    RemoveUnused(Candidates,
        Error,
        [this, &Candidates, &SourceTimes, &Error]
        {
            TArray<FBrowserCatalogRecord> Current;
            return CanContinue() && FBrowserCatalogProvider::ReadCatalog(Current, Error)
                   && ValidateCleanupCatalog(Current, Candidates, Error)
                   && ValidateCleanupSourceTimes(SourceTimes, Error);
        });
    RefreshBrowser();
    ShowResult(Error);
}
