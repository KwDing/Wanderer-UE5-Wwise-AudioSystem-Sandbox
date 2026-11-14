#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/Package.h"

#include "AkAudioEvent.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"
#include "Tests/Support/WandererWwiseSyncTestReference.h"

namespace
{
    FString GetFixtureFile(const FString& InPackage)
    {
        return FPaths::ConvertRelativePathToFull(
            FPackageName::LongPackageNameToFilename(
                InPackage, FPackageName::GetAssetPackageExtension()));
    }

    FBrowserCatalogRecord MakeEventRecord(const FString& InPackage)
    {
        FBrowserCatalogRecord Record;
        Record.GUID = FGuid::NewGuid();
        Record.ObjectPath =
            TEXT("/Events/Test/") + FPackageName::GetLongPackageAssetName(InPackage);
        Record.Package = InPackage;
        Record.ClassPath = UAkAudioEvent::StaticClass()->GetClassPathName();
        Record.bSourceEvent = true;
        return Record;
    }

    void RemoveFixturePackages(IAssetRegistry& InOutRegistry, const FString& InRoot)
    {
        TArray<FAssetData> Assets;
        InOutRegistry.GetAssetsByPath(FName(*InRoot), Assets, true);
        TArray<UObject*> Objects;
        for (const FAssetData& Asset : Assets)
        {
            if (UObject* Object = Asset.GetAsset())
            {
                Objects.Add(Object);
            }
        }
        ObjectTools::DeleteObjectsUnchecked(Objects);
        InOutRegistry.RemovePath(InRoot);
    }

    bool VerifySavedReference(FAutomationTestBase& InOutTest,
        UPackage& InOutPackage,
        const FSoftObjectPath& InReferenceSoftRef,
        const FSoftObjectPath& InCanonicalSoftRef)
    {
        if (!InOutTest.TestTrue(TEXT("Reference package unloads"),
                UPackageTools::UnloadPackages({&InOutPackage})))
        {
            return false;
        }
        const UWandererWwiseSyncTestReference* Reloaded =
            Cast<UWandererWwiseSyncTestReference>(InReferenceSoftRef.TryLoad());
        if (!InOutTest.TestNotNull(TEXT("Reference package reloads from disk"), Reloaded))
        {
            return false;
        }
        InOutTest.TestTrue(TEXT("Reloaded hard reference points at the migrated Event"),
            FSoftObjectPath(Reloaded->HardEvent.Get()) == InCanonicalSoftRef);
        InOutTest.TestTrue(TEXT("Reloaded soft reference resolves to the migrated Event"),
            Reloaded->SoftEvent.LoadSynchronous() == Reloaded->HardEvent.Get());
        return true;
    }

    bool VerifyUnreferencedCleanup(FAutomationTestBase& InOutTest,
        IAssetRegistry& InOutRegistry,
        const FString& InCanonicalRoot,
        const FString& InSearchRoot)
    {
        const FBrowserCatalogRecord Orphan =
            MakeEventRecord(InCanonicalRoot / TEXT("Events/Orphan"));
        FString Result;
        if (!FWwiseAssetSyncOperation::SyncRecord(Orphan, InCanonicalRoot, Result, InSearchRoot))
        {
            InOutTest.AddError(Result);
            return false;
        }
        const FAssetData Asset =
            InOutRegistry.GetAssetByObjectPath(FSoftObjectPath(Orphan.Package + TEXT(".Orphan")));
        InOutTest.TestFalse(TEXT("Revalidation after backup can cancel deletion"),
            FBrowserSyncController::RemoveUnused({Asset}, Result, [] { return false; }));
        InOutTest.TestTrue(TEXT("Cancellation preserves the saved Event"),
            FPaths::FileExists(GetFixtureFile(Orphan.Package)));
        if (!InOutTest.TestTrue(TEXT("Unreferenced missing Event can be removed"),
                FBrowserSyncController::RemoveUnused({Asset}, Result)))
        {
            InOutTest.AddError(Result);
            return false;
        }
        return InOutTest.TestFalse(TEXT("Deleted Event package is gone"),
            FPaths::FileExists(GetFixtureFile(Orphan.Package)));
    }

    bool VerifyDuplicateConsolidation(FAutomationTestBase& InOutTest,
        IAssetRegistry& InOutRegistry,
        const FBrowserCatalogRecord& InCanonical,
        const FString& InCanonicalRoot,
        const FString& InSearchRoot)
    {
        const FString DuplicateRoot = InSearchRoot / TEXT("Duplicate");
        FBrowserCatalogRecord Duplicate = InCanonical;
        Duplicate.Package = DuplicateRoot / TEXT("Play_Test");
        FString Result;
        if (!FWwiseAssetSyncOperation::SyncRecord(Duplicate, DuplicateRoot, Result, DuplicateRoot))
        {
            InOutTest.AddError(Result);
            return false;
        }
        const FSoftObjectPath DuplicateSoftRef(Duplicate.Package + TEXT(".Play_Test"));
        const FSoftObjectPath CanonicalSoftRef(InCanonical.Package + TEXT(".Play_Test"));
        FBrowserCatalogRecord Updated = InCanonical;
        Updated.ObjectPath = TEXT("/Events/Test/Play_New");
        if (!InOutTest.TestTrue(TEXT("Canonical sync merges a single same-GUID duplicate"),
                FWwiseAssetSyncOperation::SyncRecord(
                    Updated, InCanonicalRoot, Result, InSearchRoot)))
        {
            InOutTest.AddError(Result);
            return false;
        }
        const FAssetData Previous = InOutRegistry.GetAssetByObjectPath(DuplicateSoftRef);
        InOutTest.TestTrue(TEXT("No second Wwise Event remains"),
            !Previous.IsValid() || Previous.IsRedirector());
        const UAkAudioEvent* Canonical = Cast<UAkAudioEvent>(CanonicalSoftRef.ResolveObject());
        InOutTest.TestTrue(TEXT("The same sync refreshes the canonical Event binding"),
            Canonical && Canonical->GetWwiseName() == TEXT("Play_New"));
        return InOutTest.TestTrue(TEXT("Duplicate path resolves to the canonical Event"),
            DuplicateSoftRef.TryLoad() == CanonicalSoftRef.ResolveObject());
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWwiseAssetMigrationTest,
    "Wanderer.WwiseBrowserSync.MigrationAndReferenceProtection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWwiseAssetMigrationTest::RunTest(const FString& InParameters)
{
    const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/WwiseMigrationTest_") + RunID;
    const FString CanonicalRoot = Root / TEXT("WwiseAudio");
    const FString Directory = FPaths::ConvertRelativePathToFull(
                                  FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/Tests")
                                  / RunID)
                              + TEXT("/");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPackageName::RegisterMountPoint(Root + TEXT("/"), Directory);
    ON_SCOPE_EXIT
    {
        FPackageName::UnRegisterMountPoint(Root + TEXT("/"), Directory);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    };
    if (!TestTrue(TEXT("No fixture writes to Content"),
            FPaths::IsUnderDirectory(GetFixtureFile(Root / TEXT("MountProbe")), Directory)))
    {
        return false;
    }
    IAssetRegistry& Registry = FWwiseAssetRepository::GetRegistry();
    ON_SCOPE_EXIT
    {
        RemoveFixturePackages(Registry, Root);
    };

    FBrowserCatalogRecord Event = MakeEventRecord(Root / TEXT("Wrong/Play_Test"));
    FString Result;
    if (!FWwiseAssetSyncOperation::SyncRecord(Event, Root, Result, Root))
    {
        AddError(Result);
        return false;
    }
    const FSoftObjectPath OldSoftRef(Event.Package + TEXT(".Play_Test"));
    UAkAudioEvent* Old = Cast<UAkAudioEvent>(OldSoftRef.TryLoad());
    if (!TestNotNull(TEXT("Original Event exists"), Old))
    {
        return false;
    }
    UPackage* ReferencePackage = CreatePackage(*(Root / TEXT("Reference")));
    UWandererWwiseSyncTestReference* Reference =
        NewObject<UWandererWwiseSyncTestReference>(ReferencePackage,
            TEXT("Reference"),
            RF_Public | RF_Standalone);
    Reference->HardEvent = Old;
    Reference->SoftEvent = Old;
    FAssetRegistryModule::AssetCreated(Reference);
    const FSoftObjectPath ReferenceSoftRef(Reference);
    if (!TestTrue(TEXT("Save original hard and soft references"),
            FWwiseAssetRepository::Save({ReferencePackage})))
    {
        return false;
    }
    Registry.ScanPathsSynchronous({Root}, true);

    Event.Package = CanonicalRoot / TEXT("Events/Play_Test");
    if (!FWwiseAssetSyncOperation::SyncRecord(Event, CanonicalRoot, Result, Root))
    {
        AddError(Result);
        return false;
    }
    const FSoftObjectPath CanonicalSoftRef(Event.Package + TEXT(".Play_Test"));
    const FAssetData Previous = Registry.GetAssetByObjectPath(OldSoftRef);
    TestTrue(TEXT("Wrong path has either no asset or only a redirector"),
        !Previous.IsValid() || Previous.IsRedirector());
    TestTrue(TEXT("Canonical Event is the original UObject"),
        CanonicalSoftRef.ResolveObject() == Old);
    TestTrue(TEXT("Canonical package was saved"),
        FPaths::FileExists(GetFixtureFile(Event.Package)));
    if (!VerifySavedReference(*this, *ReferencePackage, ReferenceSoftRef, CanonicalSoftRef))
    {
        return false;
    }

    const FAssetData Referenced = Registry.GetAssetByObjectPath(CanonicalSoftRef);
    TestFalse(TEXT("Missing Event with saved UE references cannot be deleted"),
        FBrowserSyncController::RemoveUnused({Referenced}, Result));
    TestTrue(TEXT("Rejected delete leaves the saved Event"),
        FPaths::FileExists(GetFixtureFile(Event.Package)));
    return VerifyDuplicateConsolidation(*this, Registry, Event, CanonicalRoot, Root)
           && VerifyUnreferencedCleanup(*this, Registry, CanonicalRoot, Root);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWwiseAssetInterruptedBatchTest,
    "Wanderer.WwiseBrowserSync.InterruptedDragBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWwiseAssetInterruptedBatchTest::RunTest(const FString& InParameters)
{
    const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/WwiseDragTest_") + RunID;
    const FString Directory = FPaths::ConvertRelativePathToFull(
                                  FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/Tests")
                                  / RunID)
                              + TEXT("/");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPackageName::RegisterMountPoint(Root + TEXT("/"), Directory);
    ON_SCOPE_EXIT
    {
        FPackageName::UnRegisterMountPoint(Root + TEXT("/"), Directory);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    };
    if (!TestTrue(TEXT("No fixture writes to Content"),
            FPaths::IsUnderDirectory(GetFixtureFile(Root / TEXT("MountProbe")), Directory)))
    {
        return false;
    }
    IAssetRegistry& Registry = FWwiseAssetRepository::GetRegistry();
    ON_SCOPE_EXIT
    {
        RemoveFixturePackages(Registry, Root);
    };
    const FBrowserCatalogRecord First = MakeEventRecord(Root / TEXT("First"));
    const FBrowserCatalogRecord Second = MakeEventRecord(Root / TEXT("Second"));
    TArray<FAssetData> Assets;
    FString Result;
    int32 ContinuationChecks = 0;
    TestFalse(TEXT("Batch stops when its generation changes"),
        FWwiseAssetSyncOperation::PrepareDragAssets({First, Second},
            Root,
            Assets,
            Result,
            [&ContinuationChecks]
            {
                return ++ContinuationChecks == 1;
            },
            Root));
    TestTrue(TEXT("First committed record survives cancellation"),
        FPaths::FileExists(GetFixtureFile(First.Package)));
    TestFalse(TEXT("Later record is not created"),
        FPaths::FileExists(GetFixtureFile(Second.Package)));
    TestTrue(TEXT("Cancelled drag returns no asset payload"), Assets.IsEmpty());
    return true;
}

#endif
