#pragma once

#include "CoreMinimal.h"

class IAssetRegistry;
class UAkAudioType;
class UPackage;
struct FAssetData;
struct FBrowserCatalogRecord;

// Synchronous editor-thread package operations shared by import, replacement and cleanup.
class FWwiseAssetRepository
{
public:
    static const FString Root;
    // Asset queries.
    static FGuid FindAssetGUID(const FAssetData& InAsset);
    static TArray<FAssetData> FindAudioAssets(const FString& InAssetRoot);
    static IAssetRegistry& GetRegistry();
    static bool HasPackageReferencers(const FAssetData& InAsset);
    // Package persistence.
    static bool Backup(const TArray<UPackage*>& InPackages, FString& OutError);
    static bool CollectReferencingPackages(
        UPackage& InOutSource, TArray<UPackage*>& InOutPackages, FString& OutError);
    static bool Save(const TArray<UPackage*>& InPackages);

    static bool UpdateBinding(
        UAkAudioType& InOutAsset, const FBrowserCatalogRecord& InRecord, FString& OutError);
    static bool IsRetired(const FGuid& InGUID);
    static void Retire(const FGuid& InOldGUID, const FGuid& InNewGUID);

private:
    static FString GetRetirementFile();
};
