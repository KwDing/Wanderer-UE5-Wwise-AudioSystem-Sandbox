#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/RoomAudioEditOperation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAudioEditRecoveryTest,
                                 "Wanderer.Audio.Authoring.RoomEditRecovery",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FRoomAudioEditRecoveryTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AAkSpatialAudioVolume* Room = World->SpawnActor<AAkSpatialAudioVolume>();
    TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
    TStrongObjectPtr<UAkAudioEvent> Event(NewObject<UAkAudioEvent>());
    TStrongObjectPtr<UAkAudioEvent> Replacement(NewObject<UAkAudioEvent>());
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row;
    Row.Mode = EWandererWorldAudioMode::Room;
    Row.PlayEvent = Event.Get();
    Table->AddRow(TEXT("Original"), Row);
    FRoomAudioEditRequest Request;
    Request.WorldWeakPtr = World;
    Request.RoomsWeakPtr.Add(Room);
    Request.Profile.DataTable = Table.Get();
    Request.Profile.RowName = TEXT("Original");
    Request.CanContinue
        = [Room]() { return !Room->FindComponentByClass<UWandererAudioSourceComponent>(); };
    TestEqual(TEXT("Cancelled creation reports no success"),
              FRoomAudioEditOperation::Execute(Request).Applied, 0);
    TestNull(TEXT("Created record cleaned"),
             Room->FindComponentByClass<UWandererAudioSourceComponent>());
    AWandererWorldAudioBudget* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup Group;
    Group.Name = TEXT("OriginalGroup");
    Group.Rooms.Add(Room);
    Budget->ResourceGroups.Add(Group);
    Request.CanContinue = [Room, Budget]()
    {
        const UWandererAudioSourceComponent* Source =
            Room->FindComponentByClass<UWandererAudioSourceComponent>();
        if (Source && Source->ResourceGroup == TEXT("OriginalGroup"))
        {
            Budget->ResourceGroups[0].Name = TEXT("RenamedGroup");
        }
        return true;
    };
    TestEqual(TEXT("Group changed by a callback rejects stale membership"),
              FRoomAudioEditOperation::Execute(Request).Applied, 0);
    TestNull(TEXT("Stale source is removed"),
             Room->FindComponentByClass<UWandererAudioSourceComponent>());
    TestEqual(TEXT("External group edit retained"), Budget->ResourceGroups[0].Name,
              FName(TEXT("RenamedGroup")));
    Request.CanContinue = {};
    TestEqual(TEXT("Normal create"), FRoomAudioEditOperation::Execute(Request).Applied, 1);
    UWandererAudioSourceComponent* Source
        = Room->FindComponentByClass<UWandererAudioSourceComponent>();
    if (!TestNotNull(TEXT("Source exists"), Source))
    {
        return false;
    }
    Request.Action = ERoomAudioEdit::Events;
    Request.SourcesWeakPtr.Add(Source);
    Request.EventSoftRef = FSoftObjectPath(Replacement.Get());
    Request.TableSoftRef = FSoftObjectPath(Table.Get());
    bool bChanged = false;
    const FDelegateHandle Handle = Table->OnDataTableChanged().AddLambda(
        [&Table, &bChanged]()
        {
            if (bChanged)
            {
                return;
            }
            bChanged = true;
            for (FName Name : Table->GetRowNames())
            {
                if (Name != TEXT("Original"))
                {
                    FWandererWorldAudioProfileRow Changed
                        = *Table->FindRow<FWandererWorldAudioProfileRow>(Name,
                                                                         TEXT("Fault injection"));
                    Changed.PlayEvent.Reset();
                    Table->AddRow(Name, Changed);
                }
            }
        });
    const FAudioSourceResult Result = FRoomAudioEditOperation::Execute(Request);
    Table->OnDataTableChanged().Remove(Handle);
    TestTrue(TEXT("Actual table callback ran"), bChanged);
    TestEqual(TEXT("Corrupt row is not accepted"), Result.Applied, 0);
    TestEqual(TEXT("Original handle restored"), Source->Profile.RowName, FName(TEXT("Original")));
    TestEqual(TEXT("Original Room metadata retained"), Room->Room->AkAudioEvent.Get(), Event.Get());
    TestEqual(TEXT("Foreign rewritten row retained"), Table->GetRowNames().Num(), 2);
    UWandererAudioSourceComponent* Additional = NewObject<UWandererAudioSourceComponent>(
        Room, NAME_None, RF_Transactional);
    Room->AddInstanceComponent(Additional);
    Additional->Profile = Source->Profile;
    Additional->AcousticRoom = Room;
    Additional->RegisterComponent();
    Room->Room->AkAudioEvent = nullptr;
    Request.SourcesWeakPtr.Add(Additional);
    TestEqual(TEXT("Multiple layers share one batch-owned metadata write"),
        FRoomAudioEditOperation::Execute(Request).Applied, 2);
    return true;
}
#endif
