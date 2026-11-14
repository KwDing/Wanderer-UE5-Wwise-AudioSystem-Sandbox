#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"
#include "Features/WwiseBrowserExt/Catalog/WwiseEventHierarchyParser.h"
#include "AkAudioEvent.h"
#include "AkSwitchValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "IAudiokineticTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "Tests/Support/WandererWwiseSyncTestReference.h"
#include "Tests/Support/WandererWwiseTestCleanup.h"
#include "UObject/Package.h"
#include "Wwise/WwiseTreeItem.h"
#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogProvider.h"
#include "Features/WwiseBrowserExt/Assets/WwiseAssetSyncOperation.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererWwiseBrowserCatalogTest, "Wanderer.WwiseBrowserSync.CatalogAndMenus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWandererWwiseBrowserCatalogTest::RunTest(const FString& Parameters)
{
	TArray<FBrowserCatalogRecord> Catalog;
	FString Error;
	if (!TestTrue(TEXT("Read current Wwise typed catalog"), FBrowserCatalogProvider::ReadCatalog(Catalog, Error))) { AddError(Error); return false; }
	const auto Events = MakeShared<FWwiseTreeItem>(TEXT("Events"), TEXT("\\Events"), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	const auto Switches = MakeShared<FWwiseTreeItem>(TEXT("Switches"), TEXT("\\Switches"), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	int32 EventCount = 0;
	int32 SwitchCount = 0;
	for (const auto& Record : Catalog)
	{
		if (Record.ClassPath == UAkAudioEvent::StaticClass()->GetClassPathName())
		{
			++EventCount;
			TestTrue(TEXT("Event root preserved"), Record.Package.StartsWith(TEXT("/Game/WwiseAudio/Events/")));
			TestTrue(TEXT("Collapsed Events selected by prefix"), FBrowserCatalogProvider::IsSelected(Record, {Events, Switches}));
			TestFalse(TEXT("Not included by unrelated Switches selection"), FBrowserCatalogProvider::IsSelected(Record, {Switches}));
		}
		if (Record.ClassPath == UAkSwitchValue::StaticClass()->GetClassPathName())
		{
			++SwitchCount;
			TestTrue(TEXT("Switch root preserved"), Record.Package.StartsWith(TEXT("/Game/WwiseAudio/Switches/")));
			TestTrue(TEXT("Both roots support multi-selection"), FBrowserCatalogProvider::IsSelected(Record, {Events, Switches}));
		}
	}
	TestTrue(TEXT("Events found"), EventCount > 10);
	TestTrue(TEXT("Switch values found"), SwitchCount > 5);
	TestFalse(TEXT("Prefix boundary does not leak to sibling"), WwiseBrowserPaths::IsWithin(TEXT("/EventsOld/A"), TEXT("/Events")));
	auto& Tools = FModuleManager::LoadModuleChecked<IAudiokineticTools>(TEXT("AudiokineticTools"));
	TestTrue(TEXT("Native Browser context extension registered"), Tools.OnExtendWwiseBrowserContextMenu.IsBound());
	TestTrue(TEXT("Native import/drop override registered"), Tools.OnOverrideWwiseBrowserImport.IsBound());
	TestTrue(TEXT("Drag prepares persistent assets before native payload creation"), Tools.OnPrepareWwiseBrowserDrag.IsBound());
	FMenuBuilder Menu(true, nullptr);
	Tools.OnExtendWwiseBrowserContextMenu.Broadcast(Menu, {Events, Switches});
	TestTrue(TEXT("Menu builds without separate tab"), Menu.MakeWidget()->GetType() != NAME_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererWwiseBrowserSelectionTest, "Wanderer.WwiseBrowserSync.SelectionBoundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWandererWwiseBrowserSelectionTest::RunTest(const FString& Parameters)
{
	const auto Switches = MakeShared<FWwiseTreeItem>(TEXT("Switches"), TEXT("\\Switches"), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	const auto Child = MakeShared<FWwiseTreeItem>(TEXT("Footsteps"), TEXT("\\Switches\\Footsteps"), Switches, EWwiseItemType::Folder, FGuid());
	const auto Events = MakeShared<FWwiseTreeItem>(TEXT("Events"), TEXT("\\Events"), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	const auto Empty = MakeShared<FWwiseTreeItem>(TEXT("Invalid"), TEXT(""), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	const auto Global = MakeShared<FWwiseTreeItem>(TEXT("Invalid"), TEXT("\\"), nullptr, EWwiseItemType::PhysicalFolder, FGuid());
	TestEqual(TEXT("Child before parent still reduces to parent"), FBrowserCatalogProvider::ReduceSelection({Child, Switches, Child}).Num(), 1);
	TestEqual(TEXT("Two independent roots retained"), FBrowserCatalogProvider::ReduceSelection({Child, Events, Switches}).Num(), 2);
	TestEqual(TEXT("Empty and global paths rejected"), FBrowserCatalogProvider::ReduceSelection({Empty, Global, nullptr}).Num(), 0);
	TArray<FBrowserCatalogRecord> Records;
	FString Error;
	TestFalse(TEXT("No selection cannot fall back to all"), FBrowserCatalogProvider::ReadSelectedCatalog({}, Records, Error));
	TestTrue(TEXT("Empty selection has no work"), Records.IsEmpty());
	TestFalse(TEXT("Invalid folder cannot select entire catalog"), FBrowserCatalogProvider::ReadSelectedCatalog({Empty, Global}, Records, Error));
	if (!TestTrue(TEXT("Read only Switches"), FBrowserCatalogProvider::ReadSelectedCatalog({Child, Switches}, Records, Error))) { AddError(Error); return false; }
	TSet<FGuid> Unique;
	for (const auto& Record : Records)
	{
		TestTrue(TEXT("No Events or other roots in Switch selection"), Record.ObjectPath.StartsWith(TEXT("/Switches/")));
		TestFalse(TEXT("Overlapping selection creates no duplicate GUID work"), Unique.Contains(Record.GUID));
		Unique.Add(Record.GUID);
	}
	if (Records.IsEmpty()) return false;
	const auto Leaf = MakeShared<FWwiseTreeItem>(TEXT("Value"), Records[0].ObjectPath, nullptr, EWwiseItemType::Switch, Records[0].GUID);
	TestEqual(TEXT("Parent also covers explicitly selected descendant asset"), FBrowserCatalogProvider::ReduceSelection({Leaf, Switches, Child}).Num(), 1);
	TestTrue(TEXT("Single leaf catalog"), FBrowserCatalogProvider::ReadSelectedCatalog({Leaf}, Records, Error));
	TestEqual(TEXT("Single leaf does not import siblings"), Records.Num(), 1);
	TestFalse(TEXT("Empty root is never ancestor"), WwiseBrowserPaths::IsWithin(TEXT("/Events/Any"), TEXT("")));
	TestFalse(TEXT("Prefix collision is not descendant"), WwiseBrowserPaths::IsWithin(TEXT("/SwitchesExtra/Any"), TEXT("/Switches")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererWwiseBrowserAssetsTest, "Wanderer.WwiseBrowserSync.IsolatedAssetFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWandererWwiseBrowserAssetsTest::RunTest(const FString& Parameters)
{
    const FString RunID = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    // The Wwise resource cooker interprets custom top-level roots as plugin names.
    // Keep the package namespace conventional while isolating all files under Saved.
    const FString Root = TEXT("/Game/WwiseBrowserTest_") + RunID;
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/Tests") / RunID) + TEXT("/");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPackageName::RegisterMountPoint(Root + TEXT("/"), Directory);
    const FString ProbeFile = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Root / TEXT("MountProbe")));
    if (!TestTrue(TEXT("Fixture packages resolve inside Saved"), FPaths::IsUnderDirectory(ProbeFile, Directory)))
    {
        FPackageName::UnRegisterMountPoint(Root + TEXT("/"), Directory);
        return false;
    }
    auto& GetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    ON_SCOPE_EXIT
    {
        TArray<FAssetData> Assets;
        GetRegistry.GetAssetsByPath(FName(*Root), Assets, true);
        TArray<UObject*> Objects;
        for (const auto& Asset : Assets) if (UObject* Object = Asset.GetAsset()) Objects.Add(Object);
        ObjectTools::DeleteObjectsUnchecked(Objects);
        GetRegistry.RemovePath(Root);
        FPackageName::UnRegisterMountPoint(Root + TEXT("/"), Directory);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
        RemoveWwiseTestBackups(RunID);
    };
    TArray<FBrowserCatalogRecord> Catalog;
    FString Result;
    if (!FBrowserCatalogProvider::ReadCatalog(Catalog, Result)) { AddError(Result); return false; }
    TArray<FBrowserCatalogRecord> Events;
    TArray<FBrowserCatalogRecord> Switches;
    for (const auto& Record : Catalog)
    {
        if (Record.ClassPath == UAkAudioEvent::StaticClass()->GetClassPathName() && Record.Ref.IsValid())
        {
            auto Generated = Record;
            Generated.bSourceEvent = false;
            Events.Add(Generated);
        }
        if (Record.ClassPath == UAkSwitchValue::StaticClass()->GetClassPathName()) Switches.Add(Record);
    }
    if (Events.Num() < 3 || Switches.IsEmpty()) { AddError(TEXT("Need 3 actual Events and a Switch for integration test")); return false; }
    FBrowserCatalogRecord First = Events[0];
    First.Package = Root / TEXT("First");
    if (!TestTrue(TEXT("Create Event outside Content"),
            FWwiseAssetSyncOperation::SyncRecord(First, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    const FSoftObjectPath FirstPath(First.Package + TEXT(".First"));
    auto* Old = Cast<UAkAudioEvent>(FirstPath.TryLoad());
    TestNotNull(TEXT("Created Event loaded"), Old);
    UPackage* ReferencePackage = CreatePackage(*(Root / TEXT("References")));
    auto* Reference = NewObject<UWandererWwiseSyncTestReference>(ReferencePackage, TEXT("References"), RF_Public | RF_Standalone);
    Reference->HardEvent = Old;
    Reference->SoftEvent = Old;
    FAssetRegistryModule::AssetCreated(Reference);
    Reference->MarkPackageDirty();
    if (!UEditorLoadingAndSavingUtils::SavePackages({ReferencePackage}, false)) return false;
    GetRegistry.ScanPathsSynchronous({Root}, true);
    const FDateTime Before = IFileManager::Get().GetTimeStamp(*(Directory + TEXT("First.uasset")));
    TestTrue(TEXT("Unchanged GUID sync succeeds"),
        FWwiseAssetSyncOperation::SyncRecord(First, Root, Result, Root));
    TestEqual(TEXT("Unchanged asset not rewritten"), IFileManager::Get().GetTimeStamp(*(Directory + TEXT("First.uasset"))), Before);
    First.Package = Root / TEXT("Renamed");
    if (!TestTrue(TEXT("Same GUID moves"),
            FWwiseAssetSyncOperation::SyncRecord(First, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    TestTrue(TEXT("Rename retains referenced object"), Reference->HardEvent == Old);
    First.Package = Root / TEXT("First");
    if (!TestTrue(TEXT("Rename back over own redirector"),
            FWwiseAssetSyncOperation::SyncRecord(First, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    First.Package = Root / TEXT("Renamed");
    if (!FWwiseAssetSyncOperation::SyncRecord(First, Root, Result, Root))
    {
        AddError(Result);
        return false;
    }
    FBrowserCatalogRecord Second = Events[1];
    Second.Package = First.Package;
    if (!TestTrue(TEXT("Same name new GUID rebinds in place"),
            FWwiseAssetSyncOperation::SyncRecord(Second, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    TestTrue(TEXT("Same-name replacement preserves hard reference"), Reference->HardEvent == Old && Old->GetWwiseGuid() == Second.GUID);
    FBrowserCatalogRecord TargetRecord = Events[2];
    TargetRecord.Package = Root / TEXT("Target");
    if (!FWwiseAssetSyncOperation::SyncRecord(TargetRecord, Root, Result, Root))
    {
        AddError(Result);
        return false;
    }
    const FSoftObjectPath TargetPath(TargetRecord.Package + TEXT(".Target"));
    if (!TestTrue(TEXT("Explicit reference replacement"), FWwiseAssetSyncOperation::Consolidate(FSoftObjectPath(Old), TargetPath, Result))) { AddError(Result); return false; }
    TestTrue(TEXT("Hard reference now target"), Reference->HardEvent == TargetPath.TryLoad());
    TestEqual(TEXT("Soft reference path now target"), Reference->SoftEvent.ToSoftObjectPath(), TargetPath);
    FBrowserCatalogRecord Occupant = Events[0];
    Occupant.Package = Root / TEXT("Canonical");
    if (!FWwiseAssetSyncOperation::SyncRecord(Occupant, Root, Result, Root))
    {
        AddError(Result);
        return false;
    }
    TargetRecord.Package = Occupant.Package;
    if (!TestTrue(TEXT("Existing GUID and occupied canonical name converge"),
            FWwiseAssetSyncOperation::SyncRecord(TargetRecord, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    const FSoftObjectPath CanonicalPath(TargetRecord.Package + TEXT(".Canonical"));
    TestTrue(TEXT("All references converge on canonical asset"), Reference->HardEvent == CanonicalPath.TryLoad());
    TestEqual(TEXT("Soft reference converges on canonical path"), Reference->SoftEvent.ToSoftObjectPath(), CanonicalPath);
    TestTrue(TEXT("Repeated collision sync converges without redirector conflict"),
        FWwiseAssetSyncOperation::SyncRecord(TargetRecord, Root, Result, Root));
    TArray<FAssetData> DragAssets;
    if (!TestTrue(TEXT("Drag preparation returns persistent canonical asset"),
            FWwiseAssetSyncOperation::PrepareDragAssets({TargetRecord},
                Root,
                DragAssets,
                Result,
                [] { return true; },
                Root)))
    {
        AddError(Result);
        return false;
    }
    TestEqual(TEXT("One actual drag asset"), DragAssets.Num(), 1);
    if (DragAssets.Num() == 1)
    {
        Reference->HardEvent = Cast<UAkAudioEvent>(DragAssets[0].GetAsset());
        TestTrue(TEXT("Property-slot equivalent points to saved object"), Reference->HardEvent == CanonicalPath.TryLoad() && Reference->HardEvent->GetOutermost() != GetTransientPackage());
        TestTrue(TEXT("Drag payload path is canonical"), DragAssets[0].GetSoftObjectPath() == CanonicalPath);
    }
    FBrowserCatalogRecord Switch = Switches[0];
    Switch.Package = Root / TEXT("SwitchValue");
    if (!TestTrue(TEXT("Create actual Switch value"),
            FWwiseAssetSyncOperation::SyncRecord(Switch, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    auto* SwitchAsset = Cast<UAkSwitchValue>(FSoftObjectPath(Switch.Package + TEXT(".SwitchValue")).TryLoad());
    TestTrue(TEXT("Switch Group ID preserved"), SwitchAsset && SwitchAsset->GroupValueInfo.GroupShortId == Switch.Ref.GetGroupId());
    TestTrue(TEXT("Switch value GUID preserved"), SwitchAsset && SwitchAsset->GetWwiseGuid() == Switch.GUID);
    GetRegistry.ScanPathsSynchronous({Root}, true);
    FString Skipped;
    const auto Unused = FBrowserSyncController::FindUnused(Root, {}, Skipped);
    TestTrue(TEXT("Referenced Event excluded from cleanup"), !Unused.ContainsByPredicate([CanonicalPath](const FAssetData& InAsset) { return InAsset.GetSoftObjectPath() == CanonicalPath; }));
    TestTrue(TEXT("Unreferenced missing Switch selected"), Unused.ContainsByPredicate([Switch](const FAssetData& InAsset) { return InAsset.PackageName.ToString() == Switch.Package; }));
    const auto Kept = FBrowserSyncController::FindUnused(Root, {Switch.GUID, TargetRecord.GUID}, Skipped);
    TestTrue(TEXT("Live source GUIDs not removed"), Kept.IsEmpty());
    FBrowserCatalogRecord SourceOnly;
    SourceOnly.GUID = FGuid::NewGuid();
    SourceOnly.ObjectPath = TEXT("/Events/NewUnit/NewFolder/NewEvent");
    SourceOnly.Package = Root / TEXT("NewUnit/NewFolder/NewEvent");
    SourceOnly.ClassPath = UAkAudioEvent::StaticClass()->GetClassPathName();
    SourceOnly.bSourceEvent = true;
    if (!TestTrue(TEXT("Saved Event can sync without generated bank entry"),
            FWwiseAssetSyncOperation::SyncRecord(SourceOnly, Root, Result, Root)))
    {
        AddError(Result);
        return false;
    }
    auto* SourceEvent = Cast<UAkAudioEvent>(FSoftObjectPath(SourceOnly.Package + TEXT(".NewEvent")).TryLoad());
    TestTrue(TEXT("Source identity populated"), SourceEvent && SourceEvent->GetWwiseGuid() == SourceOnly.GUID && SourceEvent->GetWwiseName() == FName(TEXT("NewEvent")));
    SourceOnly.ObjectPath = TEXT("/Events/MovedUnit/RenamedEvent");
    SourceOnly.Package = Root / TEXT("MovedUnit/RenamedEvent");
    TestTrue(TEXT("Source-only Event moves and renames"),
        FWwiseAssetSyncOperation::SyncRecord(SourceOnly, Root, Result, Root));
    TestTrue(TEXT("Move preserves object identity and updates Wwise name"), SourceEvent && SourceEvent->GetPathName() == SourceOnly.Package + TEXT(".RenamedEvent") && SourceEvent->GetWwiseName() == FName(TEXT("RenamedEvent")));
    if (!TestTrue(TEXT("Delete only unreferenced candidate via UE API"), FBrowserSyncController::RemoveUnused(Unused, Result))) { AddError(Result); return false; }
    TestFalse(TEXT("Deleted Switch file removed"), FPaths::FileExists(Directory + TEXT("SwitchValue.uasset")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererWwiseEventSourceTest, "Wanderer.WwiseBrowserSync.SavedEventHierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWandererWwiseEventSourceTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("WwiseBrowserSync/SourceTests") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*Directory, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Directory, false, true); };
	const FString File = Directory / TEXT("Unit.wwu");
	const FString EventID = TEXT("{00000001-0000-0000-0000-000000000001}");
	const FString UnitID = TEXT("{00000002-0000-0000-0000-000000000002}");
	auto WriteUnit = [&File, &UnitID, &EventID](const FString& InName, bool InbHasEvent)
	{
		return FFileHelper::SaveStringToFile(TEXT("<WwiseDocument><Events><WorkUnit Name=\"") + InName + TEXT("\" ID=\"") + UnitID + TEXT("\"><ChildrenList>")
			+ (InbHasEvent ? TEXT("<Folder Name=\"Nested\" ID=\"{00000003-0000-0000-0000-000000000003}\"><ChildrenList><Event Name=\"Play\" ID=\"") + EventID + TEXT("\"><ChildrenList><Action Name=\"Ignore\"/></ChildrenList></Event></ChildrenList></Folder>") : TEXT(""))
			+ TEXT("</ChildrenList></WorkUnit></Events></WwiseDocument>"), *File);
	};
	TMap<FGuid, FString> Paths;
	FString Error;
	FGuid GUID;
	FGuid::Parse(EventID, GUID);
	TestFalse(TEXT("Missing Events snapshot fails closed"), FWwiseEventHierarchyParser::Parse(Directory, Paths, Error));
	if (!WriteUnit(TEXT("Old"), true) || !FWwiseEventHierarchyParser::Parse(Directory, Paths, Error)) { AddError(Error); return false; }
	TestEqual(TEXT("Full relative hierarchy includes WorkUnit and Folder"), Paths.FindRef(GUID), FString(TEXT("/Events/Old/Nested/Play")));
	if (!WriteUnit(TEXT("Moved"), true) || !FWwiseEventHierarchyParser::Parse(Directory, Paths, Error)) return false;
	TestEqual(TEXT("Saved move overrides prior path without Banks"), Paths.FindRef(GUID), FString(TEXT("/Events/Moved/Nested/Play")));
	if (!WriteUnit(TEXT("Moved"), false)) return false;
	TestTrue(TEXT("Valid zero-Event hierarchy accepted"), FWwiseEventHierarchyParser::Parse(Directory, Paths, Error));
	TestTrue(TEXT("Deleted Event absent even when old generated entry exists"), Paths.IsEmpty());
	const FString ChildFile = Directory / TEXT("Child.wwu");
	const FString ChildID = TEXT("{00000004-0000-0000-0000-000000000004}");
	FFileHelper::SaveStringToFile(TEXT("<WwiseDocument><Events><WorkUnit Name=\"Child\" ID=\"") + ChildID + TEXT("\" OwnerID=\"") + UnitID
		+ TEXT("\"><ChildrenList><Event Name=\"Play\" ID=\"") + EventID + TEXT("\"/></ChildrenList></WorkUnit></Events></WwiseDocument>"), *ChildFile);
	TestTrue(TEXT("Separate nested WorkUnit resolves OwnerID"), FWwiseEventHierarchyParser::Parse(Directory, Paths, Error));
	TestEqual(TEXT("Nested WorkUnit includes owning hierarchy"), Paths.FindRef(GUID), FString(TEXT("/Events/Moved/Child/Play")));
	FFileHelper::SaveStringToFile(TEXT("<WwiseDocument><Events><WorkUnit Name=\"Child\" ID=\"") + ChildID
		+ TEXT("\" OwnerID=\"{99999999-0000-0000-0000-000000000000}\"><ChildrenList><Event Name=\"Play\" ID=\"") + EventID + TEXT("\"/></ChildrenList></WorkUnit></Events></WwiseDocument>"), *ChildFile);
	TestFalse(TEXT("Unresolved parent fails closed"), FWwiseEventHierarchyParser::Parse(Directory, Paths, Error));
	IFileManager::Get().Delete(*ChildFile);
	FFileHelper::SaveStringToFile(TEXT("<broken"), *File);
	TestFalse(TEXT("Malformed source fails closed"), FWwiseEventHierarchyParser::Parse(Directory, Paths, Error));
	return true;
}
#endif
