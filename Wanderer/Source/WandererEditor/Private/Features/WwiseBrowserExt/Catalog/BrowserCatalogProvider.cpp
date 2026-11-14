#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"

#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"

#include "AkAudioEvent.h"
#include "AkUnrealAssetDataHelper.h"
#include "Wwise/WwiseProjectDatabase.h"
#include "Wwise/WwiseReconcile.h"
#include "Wwise/WwiseTreeItem.h"
#include "WwiseUnrealHelper.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"
#include "Features/WwiseBrowserExt/Catalog/WwiseEventHierarchyParser.h"


namespace
{
    void CollectFormerFolderEvents(
        const TArray<FString>& InSelectedFolders, TSet<FGuid>& InOutFormerFolderEvents)
    {
        for (const FAssetData& Asset :
            FWwiseAssetRepository::FindAudioAssets(FWwiseAssetRepository::Root))
        {
            const FString Package = Asset.PackageName.ToString();
            if (InSelectedFolders.ContainsByPredicate(
                    [&Package](const FString& InFolder)
                    {
                        return WwiseBrowserPaths::IsWithin(Package, InFolder);
                    }))
            {
                InOutFormerFolderEvents.Add(FWwiseAssetRepository::FindAssetGUID(Asset));
            }
        }
    }

} // namespace

namespace
{
    bool FillGeneratedRecord(FBrowserCatalogRecord& InOutRecord,
        const WwiseAnyRef& InRef,
        UClass& InOutAssetClass,
        FString& OutError)
    {
        TArray<FString> Parts;
        InOutRecord.ObjectPath.ParseIntoArray(Parts, TEXT("/"), false);
        if (Parts.Contains(TEXT("..")) || Parts.Contains(TEXT("."))
            || InOutRecord.ObjectPath.Contains(TEXT("//")))
        {
            OutError = TEXT("无效 Wwise 路径：") + InOutRecord.ObjectPath;
            return false;
        }
        const FString Name = AkUnrealAssetDataHelper::GetAssetDefaultName(&InRef).ToString();
        InOutRecord.Package = UPackageTools::SanitizePackageName(
            FWwiseAssetRepository::Root + FPaths::GetPath(InOutRecord.ObjectPath) / Name);
        if (!FPackageName::IsValidLongPackageName(InOutRecord.Package))
        {
            OutError = TEXT("无效 UE 路径：") + InOutRecord.Package;
            return false;
        }
        InOutRecord.ClassPath = InOutAssetClass.GetClassPathName();
        InOutRecord.Ref =
            InRef; // Shared metadata ownership survives a subsequent database refresh.

        return true;
    }

    bool ReadGeneratedRecords(WwiseDataStructureScopeLock& InOutLock,
        IWwiseReconcile& InOutReconcile,
        TArray<FBrowserCatalogRecord>& OutRecords,
        const TArray<TSharedPtr<FWwiseTreeItem>>* InSelection,
        FString& OutError)
    {
        // Indices survive appends of non-Event records while the database lock is held.
        TMap<FGuid, int32> SourceEventIndices;
        for (int32 Index = 0; Index < OutRecords.Num(); ++Index)
        {
            SourceEventIndices.Add(OutRecords[Index].GUID, Index);
        }
        TSet<FGuid> Seen;
        for (const auto& Entry : InOutLock.GetCurrentPlatformData()->Guids)
        {
            WwiseDBPair<const WwiseDatabaseLocalizableGuidKey, WwiseAnyRef> Pair(Entry);
            const WwiseAnyRef& Ref = Pair.GetSecond();
            UClass* AssetClass = InOutReconcile.GetUClassFromWwiseRefType(Ref.GetType());
            if (!AssetClass)
            {
                continue;
            }
            int PartA;
            int PartB;
            int PartC;
            int PartD;
            Ref.GetGuid().GetGuidValues(PartA, PartB, PartC, PartD);
            const FGuid GUID(PartA, PartB, PartC, PartD);
            if (AssetClass == UAkAudioEvent::StaticClass())
            {
                const int32* Index = SourceEventIndices.Find(GUID);
                if (Index
                    && FPaths::GetCleanFilename(OutRecords[*Index].ObjectPath)
                           == FWwiseStringConverter::ToFString(Ref.GetName()))
                {
                    OutRecords[*Index].Ref = Ref;
                }
                continue; // Deleted/stale generated Events never enter the source catalog.
            }
            FBrowserCatalogRecord Record;
            Record.GUID = GUID;
            if (!Record.GUID.IsValid() || Seen.Contains(Record.GUID))
            {
                continue;
            }
            Record.ObjectPath = WwiseBrowserPaths::NormalizePath(
                FWwiseStringConverter::ToFString(Ref.GetObjectPath()));
            if (InSelection && !FBrowserCatalogProvider::IsSelected(Record, *InSelection))
            {
                continue;
            }
            if (!FillGeneratedRecord(Record, Ref, *AssetClass, OutError))
            {
                return false;
            }
            OutRecords.Add(MoveTemp(Record));
            Seen.Add(GUID);
        }

        return true;
    }

    bool ReadSourceEvents(TArray<FBrowserCatalogRecord>& OutRecords,
        FString& OutError,
        const TArray<TSharedPtr<FWwiseTreeItem>>* InSelection)
    {
        TMap<FGuid, FString> EventPaths;
        if (!FWwiseEventHierarchyParser::Parse(
                FPaths::GetPath(WwiseUnrealHelper::GetWwiseProjectPath()) / TEXT("Events"),
                EventPaths,
                OutError))
        {
            return false;
        }
        for (const TPair<FGuid, FString>& Event : EventPaths)
        {
            FBrowserCatalogRecord Record;
            Record.GUID = Event.Key;
            Record.ObjectPath = Event.Value;
            Record.Package =
                UPackageTools::SanitizePackageName(FWwiseAssetRepository::Root + Event.Value);
            Record.ClassPath = UAkAudioEvent::StaticClass()->GetClassPathName();
            Record.bSourceEvent = true;
            if (!FPackageName::IsValidLongPackageName(Record.Package))
            {
                OutError = TEXT("无效 UE Event 路径：") + Record.Package;
                return false;
            }
            if (!InSelection || FBrowserCatalogProvider::IsSelected(Record, *InSelection))
            {
                OutRecords.Add(MoveTemp(Record));
            }
        }
        return true;
    }
} // namespace

bool FBrowserCatalogProvider::IsSelected(
    const FBrowserCatalogRecord& InRecord, const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    for (const TSharedPtr<FWwiseTreeItem>& Item : InSelection)
    {
        if (!Item)
        {
            continue;
        }
        if (Item->ItemId.IsValid() && Item->ItemId == InRecord.GUID)
        {
            return true;
        }
        if (Item->IsFolder()
            && WwiseBrowserPaths::IsWithin(
                InRecord.ObjectPath, WwiseBrowserPaths::NormalizePath(Item->FolderPath)))
        {
            return true;
        }
    }
    return false;
}

TArray<TSharedPtr<FWwiseTreeItem>> FBrowserCatalogProvider::ReduceSelection(
    const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    TArray<TSharedPtr<FWwiseTreeItem>> Candidates;
    for (const TSharedPtr<FWwiseTreeItem>& Item : InSelection)
    {
        if (!Item)
        {
            continue;
        }
        const FString Path = WwiseBrowserPaths::NormalizePath(Item->FolderPath);
        TArray<FString> Parts;
        Path.ParseIntoArray(Parts, TEXT("/"), true);
        if (Path.IsEmpty() || Path.Contains(TEXT("//")) || Parts.Contains(TEXT(".."))
            || Parts.Contains(TEXT(".")))
        {
            continue;
        }
        if (!Item->IsFolder() && !Item->ItemId.IsValid())
        {
            continue;
        }
        Candidates.AddUnique(Item);
    }
    Candidates.Sort(
        [](const TSharedPtr<FWwiseTreeItem>& InLeft, const TSharedPtr<FWwiseTreeItem>& InRight)
        {
            return InLeft->FolderPath.Len() < InRight->FolderPath.Len();
        });
    TArray<TSharedPtr<FWwiseTreeItem>> Result;
    for (const TSharedPtr<FWwiseTreeItem>& Item : Candidates)
    {
        const FString Path = WwiseBrowserPaths::NormalizePath(Item->FolderPath);
        const bool bCovered = Result.ContainsByPredicate(
            [Item, Path](const TSharedPtr<FWwiseTreeItem>& InParent)
            {
                return (InParent->ItemId.IsValid() && InParent->ItemId == Item->ItemId)
                       || (InParent->IsFolder()
                           && WwiseBrowserPaths::IsWithin(
                               Path, WwiseBrowserPaths::NormalizePath(InParent->FolderPath)));
            });
        if (!bCovered)
        {
            Result.Add(Item);
        }
    }
    return Result;
}


bool FBrowserCatalogProvider::IsCatalogCurrent(const TArray<FBrowserCatalogRecord>& InRecords)
{
    TArray<FBrowserCatalogRecord> Current;
    FString Error;
    if (!ReadCatalog(Current, Error))
    {
        return false;
    }
    for (const FBrowserCatalogRecord& Record : InRecords)
    {
        const FBrowserCatalogRecord* Match = Current.FindByPredicate(
            [Record](const FBrowserCatalogRecord& InOther)
            {
                return Record.GUID == InOther.GUID;
            });
        if (!Match || Match->Package != Record.Package || Match->ObjectPath != Record.ObjectPath
            || Match->ClassPath != Record.ClassPath)
        {
            return false;
        }
        if (!Record.bSourceEvent
            && (Match->Ref.GetId() != Record.Ref.GetId()
                || Match->Ref.GetGroupId() != Record.Ref.GetGroupId()))
        {
            return false;
        }
    }
    return true;
}

bool FBrowserCatalogProvider::ReadCatalog(TArray<FBrowserCatalogRecord>& OutRecords,
    FString& OutError,
    const TArray<TSharedPtr<FWwiseTreeItem>>* InSelection)
{
    OutRecords.Reset();
    if (InSelection && InSelection->IsEmpty())
    {
        OutError = TEXT("未选中项目，不读取同步清单。");
        return false;
    }
    if (!ReadSourceEvents(OutRecords, OutError, InSelection))
    {
        return false;
    }
    FWwiseProjectDatabase* Database = FWwiseProjectDatabase::Get();
    IWwiseReconcile* Reconcile = IWwiseReconcile::Get();
    if (!Database || !Reconcile)
    {
        OutError = TEXT("Wwise 数据库尚未就绪。");
        return false;
    }
    WwiseDataStructureScopeLock Lock(*Database);
    if (!Lock.GetCurrentPlatformData() || Lock.GetSoundBanks().Size() == 0)
    {
        return true;
    }
    if (!ReadGeneratedRecords(Lock, *Reconcile, OutRecords, InSelection, OutError))
    {
        return false;
    }
    OutRecords.Sort(
        [](const FBrowserCatalogRecord& InLeft, const FBrowserCatalogRecord& InRight)
        {
            return InLeft.Package < InRight.Package;
        });
    return true;
}

bool FBrowserCatalogProvider::ReadSelectedCatalog(
    const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection,
    TArray<FBrowserCatalogRecord>& OutRecords,
    FString& OutError)
{
    const TArray<TSharedPtr<FWwiseTreeItem>> Roots = ReduceSelection(InSelection);
    OutRecords.Reset();
    if (Roots.IsEmpty())
    {
        OutError = TEXT("未选中有效 Wwise 对象或目录，不执行同步。请先选择需要同步的项目。");
        return false;
    }
    if (!ReadCatalog(OutRecords, OutError))
    {
        return false;
    }
    // A moved Event can still be selected through its former UE folder.
    TArray<FString> SelectedFolders;
    for (const TSharedPtr<FWwiseTreeItem>& Item : Roots)
    {
        if (Item->IsFolder())
        {
            SelectedFolders.Add(UPackageTools::SanitizePackageName(
                FWwiseAssetRepository::Root + WwiseBrowserPaths::NormalizePath(Item->FolderPath)));
        }
    }
    TSet<FGuid> FormerFolderEvents;
    if (!SelectedFolders.IsEmpty())
    {
        CollectFormerFolderEvents(SelectedFolders, FormerFolderEvents);
    }
    OutRecords.RemoveAll(
        [&Roots, &FormerFolderEvents](const FBrowserCatalogRecord& InRecord)
        {
            return !IsSelected(InRecord, Roots)
                   && !(InRecord.bSourceEvent && FormerFolderEvents.Contains(InRecord.GUID));
        });
    return true;
}
