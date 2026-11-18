#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkAudioType.h"
#include "AkInitBank.h"
#include "WwiseUnrealHelper.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"
#include "Features/WwiseBrowserExt/Catalog/WwiseEventHierarchyParser.h"

namespace
{
    bool bSyncInProgress = false;

    TArray<FAssetData> FindMatchingAssets(
        const FBrowserCatalogRecord& InRecord, const FString& InSearchRoot)
    {
        TArray<FAssetData> Matches = FWwiseAssetRepository::FindAudioAssets(InSearchRoot);
        Matches.RemoveAll(
            [&InRecord](const FAssetData& InAsset)
            {
                return InAsset.AssetClassPath != InRecord.ClassPath
                       || FWwiseAssetRepository::FindAssetGUID(InAsset) != InRecord.GUID;
            });
        return Matches;
    }

    bool ValidateOccupiedEvent(
        const FAssetData& InOccupied, const FBrowserCatalogRecord& InRecord, FString& OutResult)
    {
        TMap<FGuid, FString> Paths;
        if (!FWwiseEventHierarchyParser::Parse(
                FPaths::GetPath(WwiseUnrealHelper::GetWwiseProjectPath()) / TEXT("Events"),
                Paths,
                OutResult))
        {
            return false;
        }
        if (Paths.Contains(FWwiseAssetRepository::FindAssetGUID(InOccupied)))
        {
            OutResult =
                TEXT("目标被另一个仍存在的 Event 占用，请先同步该对象的移动：") + InRecord.Package;
            return false;
        }
        return true;
    }

    bool CreateRecordAsset(
        const FBrowserCatalogRecord& InRecord, const FString& InName, FString& OutResult)
    {
        if (FPackageName::DoesPackageExist(InRecord.Package))
        {
            OutResult = TEXT("目标文件存在但注册表未识别，请刷新：") + InRecord.Package;
            return false;
        }
        UClass* AssetClass = FindObject<UClass>(nullptr, *InRecord.ClassPath.ToString());
        if (!AssetClass)
        {
            OutResult = TEXT("资产类型不可用：") + InRecord.ClassPath.ToString();
            return false;
        }
        UPackage* Package = CreatePackage(*InRecord.Package);
        UAkAudioType* Created = NewObject<UAkAudioType>(
            Package, AssetClass, FName(*InName), RF_Public | RF_Standalone | RF_Transactional);
        if (Created->IsA<UAkAudioEvent>())
        {
            Created->bAutoLoad = false;
        }
        FAssetRegistryModule::AssetCreated(Created);
        if (!FWwiseAssetRepository::UpdateBinding(*Created, InRecord, OutResult))
        {
            return false;
        }
        OutResult = TEXT("已创建：") + InRecord.Package;
        return true;
    }

    bool BindOccupiedAsset(const FAssetData& InOccupied,
        const FSoftObjectPath& InExistingSoftRef,
        const FBrowserCatalogRecord& InRecord,
        FString& OutResult)
    {
        if (InOccupied.AssetClassPath != InRecord.ClassPath)
        {
            OutResult = TEXT("同名但类型不同，保留：") + InRecord.Package;
            return false;
        }
        const TStrongObjectPtr<UAkAudioType> Canonical(Cast<UAkAudioType>(InOccupied.GetAsset()));
        if (!Canonical || !FWwiseAssetRepository::Backup({Canonical->GetOutermost()}, OutResult))
        {
            return false;
        }
        const FGuid OldGUID = Canonical->GetWwiseGuid();
        if (!FWwiseAssetRepository::UpdateBinding(*Canonical, InRecord, OutResult))
        {
            return false;
        }
        // Keep the canonical package and move references only after rebinding has succeeded.
        if (!InExistingSoftRef.IsNull()
            && !FWwiseAssetSyncOperation::Consolidate(
                InExistingSoftRef, InOccupied.GetSoftObjectPath(), OutResult))
        {
            OutResult = TEXT("部分完成：规范路径绑定已更新，但引用合并失败，可能存在重复 GUID。"
                             "请检查引用或按 Saved/WwiseBrowserSync/Backups 清单恢复。\n")
                        + OutResult;
            return false;
        }
        if (WwiseBrowserPaths::IsWithin(InRecord.Package, FWwiseAssetRepository::Root))
        {
            FWwiseAssetRepository::Retire(OldGUID, InRecord.GUID);
        }
        if (InExistingSoftRef.IsNull())
        {
            OutResult = TEXT("同名替换，保留原资产及全部引用：") + InRecord.Package;
        }
        return true;
    }

    bool UpdateExistingAsset(UAkAudioType& InOutAsset,
        const FBrowserCatalogRecord& InRecord,
        const FString& InName,
        FString& OutResult)
    {
        const bool bMove = InOutAsset.GetOutermost()->GetName() != InRecord.Package
                           || InOutAsset.GetName() != InName;
        const bool bUpdate =
            InRecord.bSourceEvent
                ? InOutAsset.GetWwiseName() != FName(*FPaths::GetCleanFilename(InRecord.ObjectPath))
                      || InOutAsset.GetShortID()
                             != FAkAudioDevice::GetShortIDFromString(
                                 FPaths::GetCleanFilename(InRecord.ObjectPath))
                : InOutAsset.IsAssetOutOfDate(InRecord.Ref);
        if (!bMove && !bUpdate)
        {
            OutResult = TEXT("跳过，无变化：") + InRecord.Package;
            return true;
        }
        if (bMove)
        {
            TArray<FAssetRenameData> Renames;
            Renames.Emplace(
                &InOutAsset, FPackageName::GetLongPackagePath(InRecord.Package), InName);
            if (!FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                    .Get()
                    .RenameAssets(Renames))
            {
                OutResult = TEXT("改名/移动失败：") + InRecord.Package;
                return false;
            }
            if (InOutAsset.GetOutermost()->GetName() != InRecord.Package
                || InOutAsset.GetName() != InName)
            {
                OutResult = TEXT("移动未到达目标路径：") + InRecord.Package;
                return false;
            }
        }
        if (!FWwiseAssetRepository::UpdateBinding(InOutAsset, InRecord, OutResult))
        {
            return false;
        }
    OutResult = TEXT("已更新/改名：") + InRecord.Package;
    if (bMove)
    {
        OutResult += TEXT("。请检查引用包保存状态，旧路径可能保留重定向器。");
    }
        return true;
    }
} // namespace

bool FWwiseAssetSyncOperation::CanMutate()
{
    check(IsInGameThread());
    return GEditor && !GEditor->PlayWorld && !GEditor->bIsSimulatingInEditor;
}

bool FWwiseAssetSyncOperation::Consolidate(const FSoftObjectPath& InOldSoftRef,
    const FSoftObjectPath& InNewSoftRef,
    FString& OutError,
    const FGuid& InExpectedSharedGUID)
{
    if (!CanMutate())
    {
        OutError = TEXT("请停止 PIE。");
        return false;
    }
    TStrongObjectPtr<UAkAudioType> Old(Cast<UAkAudioType>(InOldSoftRef.TryLoad()));
    const TStrongObjectPtr<UAkAudioType> Target(Cast<UAkAudioType>(InNewSoftRef.TryLoad()));
    if (!Old || !Target || Old.Get() == Target.Get() || Old->GetClass() != Target->GetClass()
        || Old->IsA<UAkInitBank>())
    {
        OutError = TEXT("需要两个不同且类型相同的 Wwise 资产。");
        return false;
    }
    const FGuid OldGUID = Old->GetWwiseGuid();
    const FGuid NewGUID = Target->GetWwiseGuid();
    if (InExpectedSharedGUID.IsValid()
        && (OldGUID != InExpectedSharedGUID || NewGUID != InExpectedSharedGUID))
    {
        OutError = TEXT("同 GUID 合并目标已变化，取消。");
        return false;
    }
    TArray<UPackage*> Packages{Target->GetOutermost()};
    if (!FWwiseAssetRepository::CollectReferencingPackages(
            *Old->GetOutermost(), Packages, OutError))
    {
        return false;
    }
    if (!FWwiseAssetRepository::Backup(Packages, OutError))
    {
        return false;
    }
    if (!CanMutate() || FSoftObjectPath(Old.Get()) != InOldSoftRef
        || FSoftObjectPath(Target.Get()) != InNewSoftRef || Old->GetWwiseGuid() != OldGUID
        || Target->GetWwiseGuid() != NewGUID)
    {
        OutError = TEXT("保存期间资产路径或编辑器状态发生变化，取消合并。");
        return false;
    }
    FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
        .Get()
        .RenameReferencingSoftObjectPaths(Packages, {{InOldSoftRef, InNewSoftRef}});
    TArray<UObject*> Objects{Old.Get()};
    // Consolidation must be able to delete the source after replacing its referencers.
    Old.Reset();
    const ObjectTools::FConsolidationResults Result =
        ObjectTools::ConsolidateObjects(Target.Get(), Objects, false);
    if (!Result.InvalidConsolidationObjs.IsEmpty() || !Result.FailedConsolidationObjs.IsEmpty())
    {
        OutError = TEXT("合并未完成，请检查已修改的引用及备份。");
        return false;
    }
    for (UPackage* Package : Result.DirtiedPackages)
    {
        Packages.AddUnique(Package);
    }
    if (WwiseBrowserPaths::IsWithin(InOldSoftRef.GetLongPackageName(), FWwiseAssetRepository::Root))
    {
        FWwiseAssetRepository::Retire(OldGUID, NewGUID);
    }
    if (!FWwiseAssetRepository::Save(Packages))
    {
        OutError = TEXT("引用已替换但保存失败，请保存相关包后再关闭编辑器。");
        return false;
    }
    OutError =
        TEXT("引用已替换：") + InOldSoftRef.ToString() + TEXT(" → ") + InNewSoftRef.ToString();
    return true;
}

bool FWwiseAssetSyncOperation::PrepareDragAssets(const TArray<FBrowserCatalogRecord>& InRecords,
    const FString& InAssetRoot,
    TArray<FAssetData>& OutAssets,
    FString& OutError,
    TFunctionRef<bool()> InCanContinue,
    const FString& InSearchRoot)
{
    OutAssets.Reset();
    TSet<FString> Destinations;
    for (const FBrowserCatalogRecord& Record : InRecords)
    {
        if (Destinations.Contains(Record.Package.ToLower()))
        {
            OutError = TEXT("不同源对象映射到同一目标路径，取消拖放。");
            return false;
        }
        Destinations.Add(Record.Package.ToLower());
    }
    TArray<FAssetData> PreparedAssets;
    for (const FBrowserCatalogRecord& Record : InRecords)
    {
        if (!InCanContinue())
        {
            OutError = TEXT("浏览器已停用，停止剩余拖放同步。已完成项保留。");
            return false;
        }
        if (!SyncRecord(Record, InAssetRoot, OutError, InSearchRoot))
        {
            return false;
        }
        const FString Name = FPackageName::GetLongPackageAssetName(Record.Package);
        const FAssetData Asset = FWwiseAssetRepository::GetRegistry().GetAssetByObjectPath(
            FSoftObjectPath(Record.Package + TEXT(".") + Name));
        if (!Asset.IsValid() || Asset.AssetClassPath != Record.ClassPath)
        {
            OutError = TEXT("无法取得已保存的拖放资产。");
            return false;
        }
        PreparedAssets.Add(Asset);
    }
    if (!InCanContinue())
    {
        OutError = TEXT("浏览器已停用，取消拖放。已完成项保留。");
        return false;
    }
    OutAssets = MoveTemp(PreparedAssets);
    return true;
}

bool FWwiseAssetSyncOperation::SyncRecord(const FBrowserCatalogRecord& InRecord,
    const FString& InAssetRoot,
    FString& OutResult,
    const FString& InSearchRoot)
{
    if (!CanMutate() || bSyncInProgress || !InRecord.GUID.IsValid()
        || !WwiseBrowserPaths::IsWithin(InRecord.Package, InAssetRoot)
        || !WwiseBrowserPaths::IsWithin(InAssetRoot, InSearchRoot)
        || (!InRecord.Ref.IsValid() && !InRecord.bSourceEvent))
    {
        OutResult = TEXT("请停止 PIE 并检查目标路径/生成数据。");
        return false;
    }
    IAssetRegistry& Registry = FWwiseAssetRepository::GetRegistry();
    if (Registry.IsLoadingAssets())
    {
        OutResult = TEXT("资产注册表仍在扫描，请等待后重试。");
        return false;
    }
    TGuardValue<bool> SyncGuard(bSyncInProgress, true);
    const TArray<FAssetData> Matches = FindMatchingAssets(InRecord, InSearchRoot);
    constexpr int32 MatchingAssetCountMax = 1;
    const FString Name = FPackageName::GetLongPackageAssetName(InRecord.Package);
    const FSoftObjectPath DestinationSoftRef(InRecord.Package + TEXT(".") + Name);
    const FAssetData Occupied = Registry.GetAssetByObjectPath(DestinationSoftRef);
    constexpr int32 CanonicalAndDuplicateCount = 2;
    if (Matches.Num() == CanonicalAndDuplicateCount && Occupied.IsValid()
        && Matches.Contains(Occupied))
    {
        const TStrongObjectPtr<UAkAudioType> Canonical(Cast<UAkAudioType>(Occupied.GetAsset()));
        if (!Canonical || Canonical->GetWwiseGuid() != InRecord.GUID
            || FSoftObjectPath(Canonical.Get()) != DestinationSoftRef)
        {
            OutResult = TEXT("无法核对规范资产，取消同 GUID 合并：") + InRecord.Package;
            return false;
        }
        if (!UpdateExistingAsset(*Canonical, InRecord, Name, OutResult))
        {
            return false;
        }
        const FAssetData& Duplicate = Matches[Matches[0] == Occupied ? 1 : 0];
        return Consolidate(
            Duplicate.GetSoftObjectPath(), DestinationSoftRef, OutResult, InRecord.GUID);
    }
    if (Matches.Num() > MatchingAssetCountMax)
    {
        OutResult = TEXT("重复 GUID 且无法唯一确定规范资产，请先检查并合并：") + InRecord.Package;
        return false;
    }
    TStrongObjectPtr<UAkAudioType> Existing(
        Matches.IsEmpty() ? nullptr : Cast<UAkAudioType>(Matches[0].GetAsset()));
    const bool bExistingMatches =
        Existing && Existing->GetWwiseGuid() == InRecord.GUID
        && FSoftObjectPath(Existing.Get()) == Matches[0].GetSoftObjectPath();
    if (!Matches.IsEmpty() && !bExistingMatches)
    {
        OutResult = TEXT("无法加载或核对已有 Wwise 资产，未创建副本：")
                    + Matches[0].GetObjectPathString();
        return false;
    }
    const UObjectRedirector* Redirector =
        Occupied.IsRedirector() ? Cast<UObjectRedirector>(Occupied.GetAsset()) : nullptr;
    const bool bOwnRedirector = Redirector && Existing
                                && Redirector->DestinationObject == Existing.Get();
    if (InRecord.bSourceEvent && Occupied.IsValid() && !Occupied.IsRedirector()
        && FWwiseAssetRepository::FindAssetGUID(Occupied) != InRecord.GUID)
    {
        if (!ValidateOccupiedEvent(Occupied, InRecord, OutResult))
        {
            return false;
        }
    }
    if (Occupied.IsValid() && !bOwnRedirector
        && (!Existing || Occupied.GetSoftObjectPath() != FSoftObjectPath(Existing.Get())))
    {
        const FSoftObjectPath ExistingSoftRef(Existing.Get());
        Existing.Reset();
        return BindOccupiedAsset(Occupied, ExistingSoftRef, InRecord, OutResult);
    }
    if (Existing)
    {
        return UpdateExistingAsset(*Existing, InRecord, Name, OutResult);
    }
    return CreateRecordAsset(InRecord, Name, OutResult);
}
