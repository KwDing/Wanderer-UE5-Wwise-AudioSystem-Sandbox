#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/DataTable.h"
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioEvent.h"

#include "Data/Audio/WandererWorldAudioTypes.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldAudioProfileNameTest,
    "Wanderer.Editor.AudioManager.ProfileNaming.Names",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldAudioProfileNameTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Lake default"), WorldAudioProfileUtils::SuggestRowName(TEXT("Play_Amb_Lake")),
        FString(TEXT("AMB_LAKE")));
    TestEqual(TEXT("Prefix is case insensitive"),
        WorldAudioProfileUtils::SuggestRowName(TEXT("play_Amb_Lake")), FString(TEXT("AMB_LAKE")));
    TestEqual(TEXT("Only a leading prefix is removed"),
        WorldAudioProfileUtils::SuggestRowName(TEXT("Amb_Play_Lake")), FString(TEXT("AMB_PLAY_LAKE")));
    TestFalse(TEXT("Empty rejected"), WorldAudioProfileUtils::ValidateRowName(TEXT("")).IsEmpty());
    TestFalse(TEXT("None rejected"), WorldAudioProfileUtils::ValidateRowName(TEXT("None")).IsEmpty());
    TestFalse(TEXT("Path rejected"), WorldAudioProfileUtils::ValidateRowName(TEXT("A/B")).IsEmpty());
    TestTrue(TEXT("Custom name accepted"),
        WorldAudioProfileUtils::ValidateRowName(TEXT("Lake_West")).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldAudioProfileStorageTest,
    "Wanderer.Editor.AudioManager.ProfileNaming.Storage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldAudioProfileStorageTest::RunTest(const FString& Parameters)
{
    const TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    const TStrongObjectPtr<UAkAudioEvent> Event(NewObject<UAkAudioEvent>());
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row;
    Row.PlayEvent = Event.Get();
    Table->AddRow(TEXT("LEGACY_NAME"), Row);
    const FDataTableRowHandle Created = UAudioAuthoringLibrary::FindOrAddWorldAudioProfile(
        Table.Get(), Row, Event.Get(), TEXT("AMB_LAKE"));
    TestEqual(TEXT("Explicit name wins over automatic deduplication"),
        Created.RowName, FName(TEXT("AMB_LAKE")));
    UAudioAuthoringLibrary::FindOrAddWorldAudioProfile(
        Table.Get(), Row, Event.Get(), TEXT("AMB_LAKE"));
    TestEqual(TEXT("Identical named row reused"), Table->GetRowNames().Num(), 2);
    FWandererWorldAudioProfileRow Different = Row;
    Different.Mode = EWandererWorldAudioMode::Global;
    TestFalse(TEXT("Conflict reported before mutation"),
        WorldAudioProfileUtils::ValidateRow(Table.Get(), TEXT("AMB_LAKE"), Different).IsEmpty());
    const FDataTableRowHandle Rejected = UAudioAuthoringLibrary::FindOrAddWorldAudioProfile(
        Table.Get(), Different, Event.Get(), TEXT("AMB_LAKE"));
    TestTrue(TEXT("Conflicting write rejected"), Rejected.IsNull());
    TestEqual(TEXT("No row added on conflict"), Table->GetRowNames().Num(), 2);
    TestEqual(TEXT("Original configuration retained"),
        Table->FindRow<FWandererWorldAudioProfileRow>(TEXT("AMB_LAKE"), TEXT("Test"))->Mode,
        Row.Mode);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldAudioProfileCommitTest,
    "Wanderer.Editor.AudioManager.ProfileNaming.Commit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldAudioProfileCommitTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    const TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    const TStrongObjectPtr<UAkAudioEvent> Event(NewObject<UAkAudioEvent>());
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FAudioSourceRequest Request;
    Request.World = World;
    Request.TablePath = FSoftObjectPath(Table.Get());
    Request.Profile.PlayEvent = Event.Get();
    Request.ProfileRowName = TEXT("AMB_LAKE");
    Request.Targets.Add(World->SpawnActor<AActor>());
    FWandererWorldAudioProfileRow Conflict = Request.Profile;
    Conflict.Priority += 1.0f;
    Table->AddRow(Request.ProfileRowName, Conflict);
    TestEqual(TEXT("Conflicting create applies nothing"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    TestEqual(TEXT("No additional row on failed preflight"), Table->GetRowNames().Num(), 1);
    Request.ProfileRowName = TEXT("LAKE_WEST");
    TestEqual(TEXT("New custom row commits"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 1);
    TestTrue(TEXT("Custom row exists"), Table->GetRowMap().Contains(TEXT("LAKE_WEST")));
    GEditor->UndoTransaction();
    TestFalse(TEXT("Undo removes custom row"), Table->GetRowMap().Contains(TEXT("LAKE_WEST")));
    TestTrue(TEXT("Undo retains preexisting row"), Table->GetRowMap().Contains(TEXT("AMB_LAKE")));
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
#endif
