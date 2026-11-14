#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSourceAuthoringTest, "Wanderer.Audio.Manager.SourceDialog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSourceAuthoringTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Event = NewObject<UAkAudioEvent>();
    auto* Replacement = NewObject<UAkAudioEvent>();
    const bool bOriginalAutoLoad = Event->bAutoLoad;
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FAudioSourceRequest Request; Request.World = World; Request.TablePath = FSoftObjectPath(Table);
    Request.Profile.Mode = EWandererWorldAudioMode::Room; Request.Profile.PlayEvent = Event;
    auto* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup Group; Group.Name = TEXT("City");
    for (int32 Index = 0; Index < 5; ++Index)
    {
        auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(World, AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform(FVector(Index * 1000, 0, 0))));
        if (!Room) return false;
        Room->Room->bEnable = true; Room->Room->AutoPost = false;
        Request.Targets.Add(Room); Group.Rooms.Add(Room);
    }
    Budget->ResourceGroups.Add(Group);
    TestEqual(TEXT("No source before confirm"), FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets).Num(), 0);
    auto Result = FAudioSourceAuthoringOperation::Apply(Request);
    TestEqual(TEXT("Five Room sources created"), Result.Applied, 5);
    auto Sources = FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets);
    TestEqual(TEXT("Shared identical profile"), Table->GetRowNames().Num(), 1);
    TestEqual(TEXT("Shared Event AutoLoad unchanged"), Event->bAutoLoad, bOriginalAutoLoad);
    for (const auto& Weak : Sources)
    {
        auto* Source = Weak.Get();
        TestEqual(TEXT("Room configuration valid"), Source->ValidateConfiguration(), FString());
        TestTrue(TEXT("Room source enabled"), Source->bEnabled);
        TestEqual(TEXT("Component owned by Room"), Source->GetOwner(), static_cast<AActor*>(Source->ResolveRoom()));
        TestEqual(TEXT("Resource group inherited"), Source->ResourceGroup, FName(TEXT("City")));
        TestEqual(TEXT("Metadata populated"), Source->ResolveRoom()->Room->AkAudioEvent.Get(), Event);
    }
    GEditor->UndoTransaction();
    TestEqual(TEXT("Create Undo removes all sources"), FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets).Num(), 0);
    TestEqual(TEXT("Create Undo removes added profile"), Table->GetRowNames().Num(), 0);
    GEditor->RedoTransaction();
    Sources = FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets);
    TestEqual(TEXT("Repeated create skips existing Rooms"), FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    Request.Action = EAudioSourceAction::Edit; Request.Sources = Sources; Request.Sources.Pop();
    Request.Profile.PlayEvent = Replacement; Request.Profile.Priority = 3; Request.ChangedFields.Add(TEXT("PlayEvent"));
    Result = FAudioSourceAuthoringOperation::Apply(Request);
    TestEqual(TEXT("Edit only four captured sources"), Result.Applied, 4);
    for (const auto& Source : Request.Sources)
    {
        TestEqual(TEXT("Event changed"), Source->GetProfile()->PlayEvent.Get(), Replacement);
        TestEqual(TEXT("Untouched priority retained"), Source->GetProfile()->Priority, 60.f);
    }
    TestEqual(TEXT("Unselected source original event retained"), Sources.Last()->GetProfile()->PlayEvent.Get(), Event);
    TestEqual(TEXT("Edit did not create actors"), FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets).Num(), 5);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Batch edit undo"), Sources[0]->GetProfile()->PlayEvent.Get(), Event);
    Request.ChangedFields.Reset();
    TestEqual(TEXT("Unchanged draft no mutation"), FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    Request.ChangedFields.Add(TEXT("Mode")); Request.Profile.Mode = EWandererWorldAudioMode::Spline;
    TestEqual(TEXT("Invalid shape conversion rejected"), FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    Request.Action = EAudioSourceAction::Create; Request.Targets.Reset(); Request.Sources.Reset();
    for (auto Mode : {EWandererWorldAudioMode::Point, EWandererWorldAudioMode::Global, EWandererWorldAudioMode::Area, EWandererWorldAudioMode::Spline})
    {
        auto* Target = World->SpawnActor<AActor>(); Request.Targets = {Target}; Request.Profile.Mode = Mode;
        Result = FAudioSourceAuthoringOperation::Apply(Request);
        TestEqual(TEXT("Non-Room mode created"), Result.Applied, 1);
    }
    for (TActorIterator<AWandererAudioAreaEmitter> It(World); It; ++It)
    { TestTrue(TEXT("Area initial geometry valid"), It->IsShapeValid()); TestFalse(TEXT("Area awaits geometry editing"), It->AudioSource->bEnabled); }
    for (TActorIterator<AWandererAudioSplineEmitter> It(World); It; ++It)
    { TestTrue(TEXT("Spline initial geometry valid"), It->GetGeometryError().IsEmpty()); TestFalse(TEXT("Spline awaits geometry editing"), It->AudioSource->bEnabled); }
    Request.World.Reset();
    TestEqual(TEXT("Expired world rejected"), FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSourceShapeBindingTest,
    "Wanderer.Audio.Manager.ShapeBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioSourceShapeBindingTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AWandererAudioSplineEmitter* Spline = World->SpawnActor<AWandererAudioSplineEmitter>();
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    UAkAudioEvent* Event = NewObject<UAkAudioEvent>();
    FAudioSourceRequest Request;
    Request.World = World;
    Request.Targets = {Spline};
    Request.TablePath = FSoftObjectPath(Table);
    Request.Profile.PlayEvent = Event;
    Request.ProfileRowName = TEXT("MANUAL_SPLINE");
    UWandererAudioSourceComponent* Source = Spline->AudioSource;
    const bool bWasEnabled = Source->bEnabled;

    TestEqual(TEXT("Create binds the existing spline source"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 1);
    if (!TestNotNull(TEXT("Existing source has a profile"), Source->GetProfile()))
    {
        return false;
    }
    TestTrue(TEXT("Existing geometry selects spline mode"),
        Source->GetProfile()->Mode == EWandererWorldAudioMode::Spline);
    TestEqual(TEXT("Binding preserves enable state"), Source->bEnabled, bWasEnabled);
    TestEqual(TEXT("Explicit row is used"), Source->Profile.RowName, Request.ProfileRowName);
    TestEqual(TEXT("No duplicate source"),
        FAudioSourceAuthoringOperation::GatherSources(World, Request.Targets).Num(), 1);
    TestEqual(TEXT("Second create rejects the configured source"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 0);
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores the placeholder"), Source->Profile.IsNull());
    TestEqual(TEXT("Undo removes the new row"), Table->GetRowMap().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSourceBatchProfileNamingTest,
    "Wanderer.Audio.Manager.BatchProfileNaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioSourceBatchProfileNamingTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    AWandererAudioSplineEmitter* First = World->SpawnActor<AWandererAudioSplineEmitter>();
    AWandererAudioSplineEmitter* Second = World->SpawnActor<AWandererAudioSplineEmitter>();
    UAkAudioEvent* Replacement = NewObject<UAkAudioEvent>();

    FWandererWorldAudioProfileRow Original;
    Original.Mode = EWandererWorldAudioMode::Spline;
    Original.PlayEvent = NewObject<UAkAudioEvent>();
    constexpr float SecondPriority = 30.0f;
    Table->AddRow(TEXT("ORIGINAL_A"), Original);
    Original.Priority = SecondPriority;
    Table->AddRow(TEXT("ORIGINAL_B"), Original);
    Table->AddRow(TEXT("AMB_TEST_2"), Original);
    First->AudioSource->Profile.DataTable = Table;
    First->AudioSource->Profile.RowName = TEXT("ORIGINAL_A");
    Second->AudioSource->Profile.DataTable = Table;
    Second->AudioSource->Profile.RowName = TEXT("ORIGINAL_B");

    FAudioSourceRequest Request;
    Request.Action = EAudioSourceAction::Edit;
    Request.World = World;
    Request.TablePath = FSoftObjectPath(Table);
    Request.Sources = {First->AudioSource.Get(), Second->AudioSource.Get()};
    Request.Profile.PlayEvent = Replacement;
    Request.ProfileRowName = TEXT("AMB_TEST");
    Request.ChangedFields.Add(GET_MEMBER_NAME_CHECKED(FWandererWorldAudioProfileRow, PlayEvent));
    TestEqual(TEXT("Both differing profiles are updated"),
        FAudioSourceAuthoringOperation::Apply(Request).Applied, 2);
    TestEqual(TEXT("First profile uses the requested name"),
        First->AudioSource->Profile.RowName, FName(TEXT("AMB_TEST")));
    TestEqual(TEXT("An occupied suffix is skipped"),
        Second->AudioSource->Profile.RowName, FName(TEXT("AMB_TEST_3")));
    if (TestNotNull(TEXT("Second profile is bound"), Second->AudioSource->GetProfile()))
    {
        TestEqual(TEXT("Unedited priority is retained"),
            Second->AudioSource->GetProfile()->Priority, SecondPriority);
        TestEqual(TEXT("Selected event is applied"),
            Second->AudioSource->GetProfile()->PlayEvent.Get(), Replacement);
    }
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores original reference"),
        Second->AudioSource->Profile.RowName, FName(TEXT("ORIGINAL_B")));
    TestEqual(TEXT("Undo retains original rows only"), Table->GetRowMap().Num(), 3);
    return true;
}
#endif
