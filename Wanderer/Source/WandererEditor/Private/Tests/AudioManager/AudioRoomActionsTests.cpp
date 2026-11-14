#include "Features/AudioManager/Controller/AudioManagerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "AkAcousticPortal.h"
#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioRoomActionsTest, "Wanderer.Audio.Manager.RoomBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioRoomActionsTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("Editor fixture world"), World)) return false;
    auto MakeRoom = [World](const FVector& Position)
    {
        auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(World,
            AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform(Position)));
        if (Room && Room->Room) { Room->Room->AutoPost = false; Room->Room->bEnable = true; }
        return Room;
    };
    auto* First = MakeRoom(FVector::ZeroVector);
    auto* Second = MakeRoom(FVector(1000, 0, 0));
    auto* Native = MakeRoom(FVector(2000, 0, 0));
    if (!First || !Second || !Native) return false;
    Native->Room->AutoPost = true;
    auto* Portal = World->SpawnActor<AAkAcousticPortal>();
    auto* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup Group;
    Group.Name = TEXT("TestInterior"); Group.Rooms = {First, Second};
    Budget->ResourceGroups.Add(Group);
    auto* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row;
    Row.Mode = EWandererWorldAudioMode::Room;
    auto* Event = NewObject<UAkAudioEvent>();
    Row.PlayEvent = Event;
    Table->AddRow(TEXT("RoomA"), Row); Table->AddRow(TEXT("RoomB"), Row);
    Row.Mode = EWandererWorldAudioMode::Point; Table->AddRow(TEXT("Point"), Row);
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    Manager->Controller->State.RoomProfileTablePath = FSoftObjectPath(Table);
    Manager->Controller->State.RoomProfileRow = TEXT("RoomA"); Manager->Controller->RefreshRoomProfiles();
    TestEqual(TEXT("Only valid Room rows offered"), Manager->Controller->State.RoomProfileOptions.Num(), 2);
    auto Count = [World]() { int32 Num = 0; for (TActorIterator<AActor> It(World); It; ++It) { TArray<UWandererAudioSourceComponent*> Components; It->GetComponents(Components); Num += Components.Num(); } return Num; };
    auto Select = [&Manager](AActor* Actor)
    {
        Manager->Controller->State.Filter.Reset(); Manager->Controller->State.TypeFilter = TEXT("全部类型"); Manager->Controller->Refresh(); Manager->List->ClearSelection();
        for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->ActorWeakPtr.Get() == Actor) Manager->List->SetItemSelection(Entry, true);
    };
    Manager->Controller->SelectVisibleRooms();
    TestEqual(TEXT("All filtered Rooms selected, Portal excluded"), Manager->Controller->GetSelectedRooms().Num(), 3);
    Manager->Controller->CreateRoomSources();
    TestEqual(TEXT("Two missing sources created, native AutoPost skipped"), Count(), 2);
    TestEqual(TEXT("Native Room metadata populated"), First->Room->AkAudioEvent.Get(), Event);
    TestFalse(TEXT("Room AutoPost remains off"), First->Room->AutoPost);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Create Undo"), Count(), 0);
    TestNull(TEXT("Metadata Undo"), First->Room->AkAudioEvent.Get());
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Create Redo"), Count(), 2);
    Manager->Controller->CreateRoomSources();
    TestEqual(TEXT("Repeated create is idempotent"), Count(), 2);
    for (auto* Source : Manager->Controller->GetSelectedRoomSources())
    {
        TestEqual(TEXT("Room owns its component"), Source->GetOwner(), static_cast<AActor*>(Source->ResolveRoom()));
        TestEqual(TEXT("Created profile"), Source->Profile.RowName, FName(TEXT("RoomA")));
        TestEqual(TEXT("Inherited resource group"), Source->ResourceGroup, FName(TEXT("TestInterior")));
        TestNotNull(TEXT("Auto-linked Room"), Source->ResolveRoom());
    }
    Manager->Controller->State.RoomProfileRow = TEXT("RoomB"); Manager->Controller->ApplyRoomProfile();
    for (auto* Source : Manager->Controller->GetSelectedRoomSources()) TestEqual(TEXT("Batch apply"), Source->Profile.RowName, FName(TEXT("RoomB")));
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    for (auto* Source : Manager->Controller->GetSelectedRoomSources()) TestEqual(TEXT("Profile Undo"), Source->Profile.RowName, FName(TEXT("RoomA")));
    Manager->Controller->State.RoomProfileRow = TEXT("Point"); Manager->Controller->ApplyRoomProfile();
    for (auto* Source : Manager->Controller->GetSelectedRoomSources()) TestEqual(TEXT("Wrong mode rejected"), Source->Profile.RowName, FName(TEXT("RoomA")));
    auto* ReplacementEvent = NewObject<UAkAudioEvent>();
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(ReplacementEvent);
    Manager->Controller->ApplyRoomEvent();
    const int32 EventRowCount = Table->GetRowNames().Num();
    TestEqual(TEXT("Identical settings share one new row"), EventRowCount, 4);
    for (auto* Source : Manager->Controller->GetSelectedRoomSources())
        TestEqual(TEXT("Existing Rooms batch event replacement"), Source->GetProfile()->PlayEvent.Get(), ReplacementEvent);
    TestEqual(TEXT("Original shared row untouched"), Table->FindRow<FWandererWorldAudioProfileRow>(TEXT("RoomA"), TEXT("Test"))->PlayEvent.Get(), Event);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Event row Undo"), Table->GetRowNames().Num(), 3);
    for (auto* Source : Manager->Controller->GetSelectedRoomSources()) TestEqual(TEXT("Event assignment Undo"), Source->GetProfile()->PlayEvent.Get(), Event);
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Event row Redo"), Table->GetRowNames().Num(), EventRowCount);
    Manager->Controller->ApplyRoomEvent();
    TestEqual(TEXT("Repeated event reuses configuration"), Table->GetRowNames().Num(), EventRowCount);
    Select(First);
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(Event); Manager->Controller->ApplyRoomEvent();
    Select(Second);
    TestEqual(TEXT("Unselected Room retains replacement event"), Manager->Controller->GetSelectedRoomSources()[0]->GetProfile()->PlayEvent.Get(), ReplacementEvent);
    Manager->Controller->SelectVisibleRooms(); Manager->Controller->RefreshSelectedRoomEvent();
    TestTrue(TEXT("Mixed event selection visible"), Manager->Controller->State.SelectedRoomEvent.ToString().Contains(TEXT("多个不同 Event")));
    Manager->Controller->State.RoomEventPath.Reset(); Manager->Controller->ApplyRoomEvent();
    TestEqual(TEXT("Missing event does not add rows"), Table->GetRowNames().Num(), EventRowCount);
    auto* Orphan = World->SpawnActor<AWandererWorldAudioEmitter>();
    Orphan->AudioSource->Profile.DataTable = Table; Orphan->AudioSource->Profile.RowName = TEXT("RoomA");
    Select(Orphan); Manager->Controller->AssociateRoomSources();
    TestEqual(TEXT("Unique containing Room auto-link"), Orphan->AudioSource->ResolveRoom(), First);
    TestEqual(TEXT("Auto-link inherits group"), Orphan->AudioSource->ResourceGroup, FName(TEXT("TestInterior")));
    Orphan->SetActorLocation(Second->GetActorLocation()); Manager->Controller->AssociateRoomSources();
    TestEqual(TEXT("Explicit link preserved after moving"), Orphan->AudioSource->ResolveRoom(), First);
    Manager->Controller->DeleteRoomSources(false);
    TestEqual(TEXT("Selecting one emitter does not delete siblings"), Count(), 2);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Delete Undo"), Count(), 3);
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Delete Redo"), Count(), 2);
    auto* Overlap = MakeRoom(Second->GetActorLocation());
    auto* Ambiguous = World->SpawnActor<AWandererWorldAudioEmitter>(Second->GetActorLocation(), FRotator::ZeroRotator);
    Ambiguous->AudioSource->Profile.DataTable = Table; Ambiguous->AudioSource->Profile.RowName = TEXT("RoomA");
    Select(Ambiguous); Manager->Controller->AssociateRoomSources();
    TestTrue(TEXT("Overlapping candidates skipped"), Ambiguous->AudioSource->AcousticRoom.IsNull());
    Ambiguous->SetActorLocation(FVector(10000)); Manager->Controller->AssociateRoomSources();
    TestTrue(TEXT("No candidate skipped"), Ambiguous->AudioSource->AcousticRoom.IsNull());
    Ambiguous->SetActorLocation(First->GetActorLocation());
    Ambiguous->AudioSource->AcousticRoom = TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/Missing.Missing:PersistentLevel.Room")));
    Manager->Controller->AssociateRoomSources();
    TestFalse(TEXT("Unresolved explicit reference preserved"), Ambiguous->AudioSource->AcousticRoom.IsNull());
    Select(First); Manager->Controller->DeleteRoomSources(false);
    TestNull(TEXT("Only component deleted"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    auto* Restored = First->FindComponentByClass<UWandererAudioSourceComponent>();
    TestNotNull(TEXT("Component delete Undo restores instance"), Restored);
    if (Restored) { TestTrue(TEXT("Restored component registered"), Restored->IsRegistered()); TestTrue(TEXT("Instance membership restored"), First->GetInstanceComponents().Contains(Restored)); TestNotNull(TEXT("Restored profile"), Restored->GetProfile()); }
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestNull(TEXT("Component delete Redo"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    TestTrue(TEXT("Room kept"), IsValid(First)); TestTrue(TEXT("Portal kept"), IsValid(Portal));
    TestEqual(TEXT("Unrelated Room source and unresolved source kept"), Count(), 2);
    const int32 BeforeReadOnly = Count();
    Manager->Controller->State.bProjectAssets = true; Manager->Controller->DeleteRoomSources(false); Manager->Controller->CreateRoomSources(); Manager->Controller->AssociateRoomSources();
    TestEqual(TEXT("Project catalog cannot mutate"), Count(), BeforeReadOnly);
    Manager->Controller->State.bProjectAssets = false;
    Manager->Controller->State.Filter = Second->GetActorLabel(); Manager->Controller->Refresh(); Manager->Controller->SelectVisibleRooms();
    TestEqual(TEXT("Select-all honors text filter"), Manager->Controller->GetSelectedRooms().Num(), 1);
    TestTrue(TEXT("Overlap fixture exists"), IsValid(Overlap));
    Select(Second);
    auto* SecondSource = Manager->Controller->GetSelectedRoomSources()[0];
    SecondSource->Profile.RowName = TEXT("Missing");
    Select(SecondSource->GetOwner());
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(Event);
    Manager->Controller->State.RoomNightEventPath = FSoftObjectPath(ReplacementEvent);
    Manager->Controller->ApplyRoomSelection();
    TestNotNull(TEXT("Missing profile repaired"), SecondSource->GetProfile());
    if (SecondSource->GetProfile()) TestEqual(TEXT("Explicit night event applied"), SecondSource->GetProfile()->NightPlayEvent.Get(), ReplacementEvent);
    Native->Room->AutoPost = false;
    Select(Native);
    TestTrue(TEXT("New Room selection does not reuse another Room Event"), Manager->Controller->State.RoomEventPath.IsNull());
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(Event); Manager->Controller->ApplyRoomSelection();
    auto NativeSources = Manager->Controller->GetSelectedRoomSources();
    TestEqual(TEXT("One step creates missing source"), NativeSources.Num(), 1);
    if (NativeSources.Num()) TestEqual(TEXT("One step uses chosen Event"), NativeSources[0]->GetProfile()->PlayEvent.Get(), Event);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("One step create Undo"), Manager->Controller->GetSelectedRoomSources().Num(), 0);
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    Native->Room->AutoPost = true;
    Manager->Controller->ApplyRoomEvent();
    TestTrue(TEXT("Specific AutoPost failure shown"), Manager->Controller->State.LastRoomAction.Contains(TEXT("Auto Post")));
    // Two rows on the same Room must retain separate identities after a rebuild.
    auto* Additional = NewObject<UWandererAudioSourceComponent>(Second);
    Second->AddInstanceComponent(Additional);
    Additional->RegisterComponent();
    Select(Second);
    const auto SelectedRows = Manager->List->GetSelectedItems();
    TestEqual(TEXT("Two source rows on one Room"), SelectedRows.Num(), 2);
    Manager->Controller->Refresh();
    const auto RefreshedRows = Manager->List->GetSelectedItems();
    TestEqual(TEXT("Both Room rows stay selected"), RefreshedRows.Num(), 2);
    for (const auto& Item : SelectedRows)
    {
        TestTrue(TEXT("Original Room row still selected"), RefreshedRows.Contains(Item));
    }
    // Entire fixture is discarded; no package or map is saved and no PIE/playback is started.
    Manager.Reset();
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSavedRoomActionsTest, "Wanderer.Audio.Manager.SavedRoomBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSavedRoomActionsTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Load saved user map"), FEditorFileUtils::LoadMap(FPaths::ProjectContentDir() / TEXT("Maps/L_AudioSystemDemo.umap"), false, false))) return false;
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    Manager->Controller->State.Filter = TEXT("ROOM"); Manager->Controller->Refresh();
    auto FindEntry = [&Manager](const FString& Name)
    {
        const auto* Found = Manager->Controller->State.Entries.FindByPredicate([&Name](const auto& Entry) { return Entry->Cells.FindRef("Name") == Name; });
        return Found ? *Found : SAudioManagerPanel::FEntryPtr();
    };
    const auto House = FindEntry(TEXT("ACOUSTIC_ROOM__House"));
    const auto Upper = FindEntry(TEXT("ACOUSTIC_ROOM__Lobby_Upper"));
    const auto Diner = FindEntry(TEXT("ACOUSTIC_ROOM__Diner"));
    bool bUnderpassChecked = false;
    for (const auto& Entry : Manager->Controller->State.Entries)
        if (auto* Source = Entry->SourceWeakPtr.Get(); Source && Source->GetOwner()->GetActorLabel() == TEXT("AUDIO__AMB_UNDERPASS__Bridge"))
        {
            bUnderpassChecked = true;
            const auto* Room = Source->ResolveRoom();
            TestTrue(TEXT("Underpass legacy actor posts on native Room, not actor position"), Room && Room->Room && Source->GetPlaybackIdentity() == Room->Room.Get());
            AddInfo(FString::Printf(TEXT("Underpass owner=%s, Room=%s, profile=%s/%s, group=%s"), *Source->GetOwner()->GetActorLabel(), *GetNameSafe(Room), *GetNameSafe(Source->Profile.DataTable), *Source->Profile.RowName.ToString(), *Source->ResourceGroup.ToString()));
        }
    TestTrue(TEXT("Saved Underpass legacy source inspected"), bUnderpassChecked);
    if (House && Upper && Diner)
    {
        Manager->List->SetSelection(House, ESelectInfo::OnMouseClick);
        Manager->Controller->Refresh(); Manager->Controller->Refresh();
        TestTrue(TEXT("Refresh retains Shift anchor item identity"), FindEntry(TEXT("ACOUSTIC_ROOM__House")) == House);
        Manager->List->Private_ClearSelection();
        Manager->List->Private_SelectRangeFromCurrentTo(FindEntry(TEXT("ACOUSTIC_ROOM__Lobby_Upper")));
        TestEqual(TEXT("House to Upper after refresh selects five"), Manager->List->GetSelectedItems().Num(), 5);
        TestFalse(TEXT("Shift range excludes preceding Diner"), Manager->List->IsItemSelected(FindEntry(TEXT("ACOUSTIC_ROOM__Diner"))));
        Manager->List->SetSelection(FindEntry(TEXT("ACOUSTIC_ROOM__Lobby_Upper")), ESelectInfo::OnMouseClick);
        Manager->Controller->Refresh();
        Manager->List->Private_ClearSelection();
        Manager->List->Private_SelectRangeFromCurrentTo(FindEntry(TEXT("ACOUSTIC_ROOM__House")));
        TestEqual(TEXT("Reverse Shift range after refresh selects five"), Manager->List->GetSelectedItems().Num(), 5);
        Manager->List->SetItemSelection(FindEntry(TEXT("ACOUSTIC_ROOM__Diner")), true, ESelectInfo::OnMouseClick);
        Manager->Controller->Refresh();
        TestEqual(TEXT("Ctrl-style added selection retained"), Manager->List->GetSelectedItems().Num(), 6);
    }
    else AddError(TEXT("Range regression requires House, Upper and Diner"));
    Manager->List->ClearSelection();
    TArray<SAudioManagerPanel::FEntryPtr> Rooms;
    for (const auto& Entry : Manager->Controller->State.Entries)
        if (auto* Room = Cast<AAkSpatialAudioVolume>(Entry->ActorWeakPtr.Get()); Room && (Room->GetActorLabel().StartsWith(TEXT("ACOUSTIC_ROOM__House")) || Room->GetActorLabel().StartsWith(TEXT("ACOUSTIC_ROOM__Lobby")))) Rooms.Add(Entry);
    Manager->List->SetItemSelection(Rooms, true);
    TestEqual(TEXT("Five actual House/Lobby Rooms selected"), Rooms.Num(), 5);
    if (!Manager->Controller->GetSelectedBuildings().IsEmpty())
    {
        const auto Buildings = Manager->Controller->GetSelectedBuildings();
        TestEqual(TEXT("Five Rooms resolve to two shared buildings"), Manager->Controller->GetSelectedBuildings().Num(), 2);
        TestEqual(TEXT("No individual duplicate sources"), Manager->Controller->GetSelectedRoomSources().Num(), 0);
        TestTrue(TEXT("Five selected Rooms show shared membership without edit"), Manager->Controller->State.SharedRoomNotice.Contains(TEXT("House")) && Manager->Controller->State.SharedRoomNotice.Contains(TEXT("Lobby")));
        Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
        TestEqual(TEXT("Five saved Rooms keep five Room Details targets"), Manager->Details->GetSelectedObjects().Num(), 5);
        for (const auto& Object : Manager->Details->GetSelectedObjects())
            TestTrue(TEXT("No shared record exposed from saved Room entry"), Object.IsValid() && Object->IsA<AAkSpatialAudioVolume>());
        for (const auto& Entry : Rooms)
        {
            TestTrue(TEXT("Saved Room row identifies shared Event ownership"), Entry->Cells.FindRef("Status").Contains(TEXT("共用建筑底噪")));
            TestFalse(TEXT("Saved shared members have no falsely labelled own Event"), Entry->Cells.FindRef("Event").Contains(TEXT("[自身]")));
        }
        Manager->Controller->ApplyRoomSelection();
        TestEqual(TEXT("Quick Room apply does not create duplicates"), Manager->Controller->GetSelectedRoomSources().Num(), 0);
        Manager->Controller->SetPage(3);
        for (const auto& Entry : Manager->Controller->State.Entries)
            if (Buildings.Contains(Cast<UWandererBuildingAmbienceComponent>(Entry->SourceWeakPtr.Get()))) Manager->List->SetItemSelection(Entry, true);
        Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
        TestEqual(TEXT("Only building page opens two actual shared records"), Manager->Details->GetSelectedObjects().Num(), 2);
        TestTrue(TEXT("Saved building page editing enabled"), Manager->Details->IsPropertyEditingEnabled());
        Manager.Reset(); FAutomationEditorCommonUtils::CreateNewMap(); return true;
    }
    const int32 Before = Manager->Controller->GetSelectedRoomSources().Num();
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"));
    Manager->Controller->ApplyRoomSelection();
    TestEqual(TEXT("All five actual Rooms have sources"), Manager->Controller->GetSelectedRoomSources().Num(), 5);
    for (auto* Source : Manager->Controller->GetSelectedRoomSources())
    {
        TestEqual(TEXT("Real source configuration valid"), Source->ValidateConfiguration(), FString());
        TestEqual(TEXT("Real source event applied"), Source->GetProfile()->PlayEvent.ToSoftObjectPath(), Manager->Controller->State.RoomEventPath);
        TestEqual(TEXT("Real Room owns source"), Source->GetOwner(), static_cast<AActor*>(Source->ResolveRoom()));
    }
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Actual map creation reverted"), Manager->Controller->GetSelectedRoomSources().Num(), Before);
    Manager.Reset();
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
#endif
