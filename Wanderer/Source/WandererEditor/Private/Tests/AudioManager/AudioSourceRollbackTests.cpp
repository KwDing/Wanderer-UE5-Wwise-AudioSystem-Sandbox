#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"

namespace
{
    struct FAudioEditFixture
    {
        UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
        TStrongObjectPtr<UDataTable> Table{NewObject<UDataTable>()};
        TStrongObjectPtr<UAkAudioEvent> Event{NewObject<UAkAudioEvent>()};
        FAudioSourceRequest Request;

        FAudioEditFixture()
        {
            Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
            Request.World = World;
            Request.TablePath = FSoftObjectPath(Table.Get());
            Request.Profile.PlayEvent = Event.Get();
            Request.ProfileRowName = TEXT("INITIAL");
            Request.Targets.Add(World->SpawnActor<AActor>());
            Request.Targets.Add(World->SpawnActor<AActor>());
        }

        ~FAudioEditFixture()
        {
            Table->OnDataTableChanged().Clear();
            FAutomationEditorCommonUtils::CreateNewMap();
        }

        TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> FindSources() const
        {
            TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (UWandererAudioSourceComponent* Source =
                    It->FindComponentByClass<UWandererAudioSourceComponent>())
                {
                    Sources.Add(Source);
                }
            }
            return Sources;
        }

        void PrepareEdit()
        {
            FAudioSourceAuthoringOperation::Apply(Request);
            Request.Action = EAudioSourceAction::Edit;
            Request.Sources = FindSources();
            Request.ProfileRowName = TEXT("UPDATED");
            Request.Profile.Priority += 1.0f;
            Request.ChangedFields.Add(TEXT("Priority"));
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioCreateRollbackTest,
    "Wanderer.Editor.AudioManager.Rollback.CreateCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioCreateRollbackTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    AActor* Marker = Fixture.World->SpawnActor<AActor>();
    Marker->SetFlags(RF_Transactional);
    {
        const FScopedTransaction Previous(FText::FromString(TEXT("Previous user edit")));
        Marker->Modify();
        Marker->Tags.Add(TEXT("PreviousEdit"));
    }
    const TSharedRef<bool> Active = MakeShared<bool>(true);
    Fixture.Request.CanContinue = [Active]
    {
        return *Active;
    };
    Fixture.Table->OnDataTableChanged().AddLambda([Active]
    {
        *Active = false;
    });
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Cancelled batch reports no success"), Result.Applied, 0);
    TestEqual(TEXT("No orphan row"), Fixture.Table->GetRowNames().Num(), 0);
    TestEqual(TEXT("No orphan emitter"), Fixture.FindSources().Num(), 0);
    TestTrue(TEXT("Earlier user edit retained"), Marker->Tags.Contains(TEXT("PreviousEdit")));
    GEditor->UndoTransaction();
    TestFalse(TEXT("Undo still targets previous edit"),
        Marker->Tags.Contains(TEXT("PreviousEdit")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioEditRollbackTest,
    "Wanderer.Editor.AudioManager.Rollback.SecondEditCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioEditRollbackTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    Fixture.PrepareEdit();
    const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources = Fixture.Request.Sources;
    if (!TestEqual(TEXT("Two editable sources"), Sources.Num(), 2))
    {
        return false;
    }
    Fixture.Request.CanContinue = [Sources]
    {
        return Sources[0].IsValid() && Sources[1].IsValid()
            && !(Sources[0]->Profile.RowName == TEXT("UPDATED")
                && Sources[1]->Profile.RowName == TEXT("UPDATED"));
    };
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Whole submitted batch rejected"), Result.Applied, 0);
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& SourceWeakPtr : Sources)
    {
        TestEqual(TEXT("Previous handle restored"), SourceWeakPtr->Profile.RowName,
            FName(TEXT("INITIAL")));
    }
    TestEqual(TEXT("Only preexisting row retained"), Fixture.Table->GetRowNames().Num(), 1);
    GEditor->UndoTransaction();
    TestEqual(TEXT("Previous create remains undoable"), Fixture.FindSources().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioRoomRollbackTest,
    "Wanderer.Editor.AudioManager.Rollback.RoomCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioRoomRollbackTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(
        UAudioAuthoringLibrary::CreateAcousticBox(Fixture.World,
            AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform::Identity));
    if (!TestNotNull(TEXT("Room fixture"), Room))
    {
        return false;
    }
    Room->Room->bEnable = true;
    Room->Room->AutoPost = false;
    Fixture.Request.Targets = {Room};
    Fixture.Request.Profile.Mode = EWandererWorldAudioMode::Room;
    const TWeakObjectPtr<AAkSpatialAudioVolume> RoomWeakPtr = Room;
    Fixture.Request.CanContinue = [RoomWeakPtr]
    {
        return RoomWeakPtr.IsValid() && !RoomWeakPtr->Room->AkAudioEvent;
    };
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Room batch cancelled"), Result.Applied, 0);
    TestNull(TEXT("Room Event restored"), Room->Room->AkAudioEvent.Get());
    TestNull(TEXT("New component removed"),
        Room->FindComponentByClass<UWandererAudioSourceComponent>());
    TestEqual(TEXT("New row removed"), Fixture.Table->GetRowNames().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioReentrantCommitTest,
    "Wanderer.Editor.AudioManager.Rollback.ReentrantCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioReentrantCommitTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    const TSharedRef<int32> NestedApplied = MakeShared<int32>(INDEX_NONE);
    const FAudioSourceRequest Request = Fixture.Request;
    Fixture.Table->OnDataTableChanged().AddLambda([Request, NestedApplied]
    {
        *NestedApplied = FAudioSourceAuthoringOperation::Apply(Request).Applied;
    });
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Outer batch succeeds"), Result.Applied, 2);
    TestEqual(TEXT("Nested request rejected"), *NestedApplied, 0);
    TestEqual(TEXT("No duplicate sources"), Fixture.FindSources().Num(), 2);
    TestEqual(TEXT("Shared row created once"), Fixture.Table->GetRowNames().Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioExternalProfileTest,
    "Wanderer.Editor.AudioManager.Rollback.ExternalProfileChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioExternalProfileTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    Fixture.PrepareEdit();
    Fixture.Table->AddRow(TEXT("EXTERNAL"), Fixture.Request.Profile);
    const TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr = Fixture.Request.Sources[0];
    const TWeakObjectPtr<UDataTable> TableWeakPtr = Fixture.Table.Get();
    Fixture.Table->OnDataTableChanged().AddLambda([SourceWeakPtr, TableWeakPtr]
    {
        if (TableWeakPtr->GetRowMap().Contains(TEXT("UPDATED")) && SourceWeakPtr.IsValid())
        {
            SourceWeakPtr->Profile.RowName = TEXT("EXTERNAL");
        }
    });
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Stale plan not committed"), Result.Applied, 0);
    TestEqual(TEXT("External reference preserved"), SourceWeakPtr->Profile.RowName,
        FName(TEXT("EXTERNAL")));
    TestFalse(TEXT("Unused newly added row removed"),
        Fixture.Table->GetRowMap().Contains(TEXT("UPDATED")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioExternalRowTest,
    "Wanderer.Editor.AudioManager.Rollback.ExternalRowChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioExternalRowTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    const TWeakObjectPtr<UDataTable> TableWeakPtr = Fixture.Table.Get();
    Fixture.Table->OnDataTableChanged().AddLambda([TableWeakPtr]
    {
        FWandererWorldAudioProfileRow* Row = TableWeakPtr->FindRow<FWandererWorldAudioProfileRow>(
            TEXT("INITIAL"), TEXT("External edit"), false);
        if (Row)
        {
            Row->Priority += 1.0f;
        }
    });
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    TestEqual(TEXT("Invalidated write rejected"), Result.Applied, 0);
    TestEqual(TEXT("Created actor removed"), Fixture.FindSources().Num(), 0);
    TestTrue(TEXT("Externally changed row preserved"),
        Fixture.Table->GetRowMap().Contains(TEXT("INITIAL")));
    TestTrue(TEXT("Incomplete compensation is explicit"), Result.Errors.ContainsByPredicate(
        [](const FString& InError)
        {
            return InError.Contains(TEXT("补偿未完全完成"));
        }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioExternalRoomEventTest,
    "Wanderer.Editor.AudioManager.Rollback.ExternalRoomEvent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioExternalRoomEventTest::RunTest(const FString& InParameters)
{
    FAudioEditFixture Fixture;
    const TStrongObjectPtr<UAkAudioEvent> ExternalEvent(NewObject<UAkAudioEvent>());
    AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(
        UAudioAuthoringLibrary::CreateAcousticBox(Fixture.World,
            AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform::Identity));
    if (!TestNotNull(TEXT("Room fixture"), Room))
    {
        return false;
    }
    Room->Room->bEnable = true;
    Room->Room->AutoPost = false;
    Fixture.Request.Targets = {Room};
    Fixture.Request.Profile.Mode = EWandererWorldAudioMode::Room;
    const TWeakObjectPtr<UAkRoomComponent> RoomWeakPtr = Room->Room;
    const TWeakObjectPtr<UAkAudioEvent> EventWeakPtr = ExternalEvent.Get();
    const FDelegateHandle Handle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [RoomWeakPtr, EventWeakPtr](UObject* InObject, FPropertyChangedEvent& InEvent)
        {
            if (InObject == RoomWeakPtr.Get() && RoomWeakPtr.IsValid())
            {
                RoomWeakPtr->AkAudioEvent = EventWeakPtr.Get();
            }
        });
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(Fixture.Request);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(Handle);
    TestEqual(TEXT("Room callback conflict is not success"), Result.Applied, 0);
    TestEqual(TEXT("External Room Event retained"),
        Room->Room->AkAudioEvent.Get(), ExternalEvent.Get());
    TestNull(TEXT("Created source removed"),
        Room->FindComponentByClass<UWandererAudioSourceComponent>());
    TestEqual(TEXT("Unused new profile removed"), Fixture.Table->GetRowNames().Num(), 0);
    TestTrue(TEXT("Conflict reported"), Result.Errors.ContainsByPredicate(
        [](const FString& InError)
        {
            return InError.Contains(TEXT("补偿未完全完成"));
        }));
    return true;
}
#endif
