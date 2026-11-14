#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "AkAudioDevice.h"
#include "AkAudioType.h"
#include "Wwise/WwiseProjectDatabase.h"

#include "Features/WwiseBrowserExt/BrowserPathUtils.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"

const FString FWwiseAssetRepository::Root = TEXT("/Game/WwiseAudio");

FGuid FWwiseAssetRepository::FindAssetGUID(const FAssetData& InAsset)
{
    FGuid GUID;
    FString Value;
    if (InAsset.GetTagValue(TEXT("WwiseGuid"), Value))
    {
        FGuid::Parse(Value, GUID);
    }
    return GUID;
}

TArray<FAssetData> FWwiseAssetRepository::FindAudioAssets(const FString& InAssetRoot)
{
    TArray<FAssetData> All;
    GetRegistry().GetAssetsByClass(UAkAudioType::StaticClass()->GetClassPathName(), All, true);
    All.RemoveAll(
        [InAssetRoot](const FAssetData& InAsset)
        {
            return !WwiseBrowserPaths::IsWithin(InAsset.PackageName.ToString(), InAssetRoot);
        });
    return All;
}

IAssetRegistry& FWwiseAssetRepository::GetRegistry()
{
    return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
}

bool FWwiseAssetRepository::HasPackageReferencers(const FAssetData& InAsset)
{
    TArray<FName> Referencers;
    GetRegistry().GetReferencers(InAsset.PackageName, Referencers);
    Referencers.Remove(InAsset.PackageName);
    return !Referencers.IsEmpty();
}

bool FWwiseAssetRepository::Backup(const TArray<UPackage*>& InPackages, FString& OutError)
{
    if (!Save(InPackages))
    {
        OutError = TEXT("保存取消或失败，操作未执行。");
        return false;
    }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/Backups")
                              / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Directory, true);
    TArray<FString> Manifest;
    for (UPackage* Package : InPackages)
    {
        FString File;
        if (!FPackageName::DoesPackageExist(Package->GetName(), &File))
        {
            OutError = TEXT("找不到已保存的包：") + Package->GetName();
            return false;
        }
        const FString BackupStem = Directory / FGuid::NewGuid().ToString(EGuidFormats::Digits);
        TArray<FString> Files{File};
        for (const FString& Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString Sidecar = FPaths::ChangeExtension(File, Extension);
            if (FPaths::FileExists(Sidecar))
            {
                Files.Add(Sidecar);
            }
        }
        for (const FString& Source : Files)
        {
            const FString Target = BackupStem + FPaths::GetExtension(Source, true);
            if (IFileManager::Get().Copy(*Target, *Source) != COPY_OK)
            {
                OutError = TEXT("备份失败：") + Source;
                return false;
            }
            Manifest.Add(Source + TEXT(" => ") + Target);
        }
    }
    if (!FFileHelper::SaveStringArrayToFile(Manifest, *(Directory / TEXT("Manifest.txt"))))
    {
        OutError = TEXT("无法保存备份清单。");
        return false;
    }
    return true;
}

bool FWwiseAssetRepository::CollectReferencingPackages(
    UPackage& InOutSource, TArray<UPackage*>& InOutPackages, FString& OutError)
{
    InOutPackages.AddUnique(&InOutSource);
    TArray<FName> Referencers;
    GetRegistry().GetReferencers(InOutSource.GetFName(), Referencers);
    Referencers.Remove(InOutSource.GetFName());
    for (const FName Referencer : Referencers)
    {
        UPackage* Package = LoadPackage(nullptr, *Referencer.ToString(), LOAD_None);
        if (!Package)
        {
            OutError = TEXT("无法加载引用包：") + Referencer.ToString();
            return false;
        }
        InOutPackages.AddUnique(Package);
    }
    return true;
}

bool FWwiseAssetRepository::Save(const TArray<UPackage*>& InPackages)
{
    if (InPackages.IsEmpty())
    {
        return true;
    }
    return FApp::IsUnattended()
               ? UEditorLoadingAndSavingUtils::SavePackages(InPackages, false)
               : FEditorFileUtils::PromptForCheckoutAndSave(InPackages, false, false)
                     == FEditorFileUtils::PR_Success;
}

bool FWwiseAssetRepository::UpdateBinding(
    UAkAudioType& InOutAsset, const FBrowserCatalogRecord& InRecord, FString& OutError)
{
    InOutAsset.Modify();
    InOutAsset.UnloadData(false);
    if (InRecord.bSourceEvent)
    {
        FWwiseObjectInfo* Info = InOutAsset.GetInfoMutable();
        if (!Info)
        {
            OutError = TEXT("Event 绑定不可写。");
            return false;
        }
        Info->WwiseGuid = InRecord.GUID;
        Info->WwiseName = FName(*FPaths::GetCleanFilename(InRecord.ObjectPath));
        Info->WwiseShortId = FAkAudioDevice::GetShortIDFromString(Info->WwiseName.ToString());
    }
    else
    {
        InOutAsset.FillInfo(InRecord.Ref);
    }
    InOutAsset.FillMetadata(FWwiseProjectDatabase::Get());
    InOutAsset.MarkPackageDirty();
    if (InOutAsset.bAutoLoad && InRecord.Ref.IsValid())
    {
        InOutAsset.LoadData();
    }
    if (!Save({InOutAsset.GetOutermost()}))
    {
        OutError = TEXT("资产已修改但未保存：") + InOutAsset.GetPathName();
        return false;
    }
    return true;
}

bool FWwiseAssetRepository::IsRetired(const FGuid& InGUID)
{
    FString Target;
    return GConfig->GetString(
        TEXT("Replacements"), *InGUID.ToString(), Target, GetRetirementFile());
}

void FWwiseAssetRepository::Retire(const FGuid& InOldGUID, const FGuid& InNewGUID)
{
    if (InOldGUID.IsValid() && InOldGUID != InNewGUID)
    {
        IFileManager::Get().MakeDirectory(
            *(FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync")), true);
        GConfig->SetString(TEXT("Replacements"),
            *InOldGUID.ToString(),
            *InNewGUID.ToString(),
            GetRetirementFile());
        GConfig->Flush(false, GetRetirementFile());
    }
}

// Records remain local to this machine, as with the previous Event sync implementation.
FString FWwiseAssetRepository::GetRetirementFile()
{
    return FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/Retired.ini");
}
