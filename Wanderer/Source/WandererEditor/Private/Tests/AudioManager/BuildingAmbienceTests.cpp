#include "Features/AudioManager/Controller/AudioManagerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "AkAudioEvent.h"
#include "AkAudioDevice.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "IDetailsView.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingAmbienceAuthoringTest, "Wanderer.Audio.Manager.BuildingAmbience", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingAmbienceAuthoringTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    auto* Event = NewObject<UAkAudioEvent>();
    auto MakeRoom = [World](float X)
    {
        auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(World, AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform(FVector(X, 0, 0))));
        Room->Room->bEnable = true; Room->Room->AutoPost = false;
        Room->Room->UnregisterComponent(); Room->Room->RegisterComponent();
        return Room;
    };
    auto* First = MakeRoom(0); auto* Second = MakeRoom(1000); auto* Third = MakeRoom(3000);
    auto CountActors = [World]() { int32 Count = 0; for (TActorIterator<AActor> It(World); It; ++It) ++Count; return Count; };
    auto Buildings = [Budget]() { TArray<UWandererBuildingAmbienceComponent*> Result; Budget->GetComponents(Result); return Result; };
    const int32 Before = CountActors();
    auto* Building = UAudioAuthoringLibrary::CreateBuildingAmbience({First, Second}, TEXT("House"), Event);
    if (!TestNotNull(TEXT("Create inline building"), Building)) return false;
    TestEqual(TEXT("No new actor"), CountActors(), Before);
    TestEqual(TEXT("One component two members"), Building->MemberRooms.Num(), 2);
    TestEqual(TEXT("Valid inline profile"), Building->ValidateConfiguration(), FString());
    TestNull(TEXT("No generated DataTable"), Building->Profile.DataTable.Get());
    TestNull(TEXT("Room does not own duplicate source"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    TestNull(TEXT("Duplicate name rejected"), UAudioAuthoringLibrary::CreateBuildingAmbience({Third}, TEXT("House"), Event));
    TestNull(TEXT("Duplicate membership rejected"), UAudioAuthoringLibrary::CreateBuildingAmbience({Second}, TEXT("Other"), Event));
    GEditor->UndoTransaction(); TestEqual(TEXT("Create Undo removes component"), Buildings().Num(), 0);
    GEditor->RedoTransaction(); Building = Buildings()[0];
    TestTrue(TEXT("Redo registration restored"), Building->IsRegistered());
    TestTrue(TEXT("Redo instance ownership"), Budget->GetInstanceComponents().Contains(Building));
    TestEqual(TEXT("Redo event preserved"), Building->BuildingEvent.Get(), Event);
    auto* Other = UAudioAuthoringLibrary::CreateBuildingAmbience({Third}, TEXT("Lobby"), Event);
    if (!Other) return false;
    // Exercise native spatial index without starting PIE or posting an Event.
    auto* System = NewObject<UWandererAudioSubsystem>(World);
    System->RegisterSource(Building); System->RegisterSource(Other);
    TestEqual(TEXT("All three member paths indexed"), System->RoomBuildingsWeakPtrs.Num(), 3);
    TestTrue(TEXT("Both member paths share the same component"), System->RoomBuildingsWeakPtrs.FindRef(FSoftObjectPath(First)).Get() == Building && System->RoomBuildingsWeakPtrs.FindRef(FSoftObjectPath(Second)).Get() == Building);
    if (FAkAudioDevice::IsInitialized())
    {
        System->UpdateBuildingListener(FVector::ZeroVector);
        TestFalse(TEXT("First sample waits for confirmation"), System->IsListenerInsideBuilding(Building));
        System->UpdateBuildingListener(FVector::ZeroVector);
        TestTrue(TEXT("Second sample confirms building"), System->IsListenerInsideBuilding(Building));
        System->UpdateBuildingListener(FVector(1000, 0, 0));
        System->UpdateBuildingListener(FVector(1000, 0, 0));
        TestTrue(TEXT("Second member retains same building"), System->IsListenerInsideBuilding(Building));
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        System->UpdateBuildingListener(FVector(1000, 0, 0));
        TestTrue(TEXT("Single boundary excursion retains building"), System->IsListenerInsideBuilding(Building));
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        TestFalse(TEXT("Other building requires a fresh pair"), System->IsListenerInsideBuilding(Other));
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        TestTrue(TEXT("Other building identified"), System->IsListenerInsideBuilding(Other));
        TestFalse(TEXT("Only one active building"), System->IsListenerInsideBuilding(Building));
        Third->Room->UnregisterComponent();
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        TestFalse(TEXT("Unloaded Room cannot activate bed"), System->IsListenerInsideBuilding(Other));
        Third->Room->RegisterComponent();
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        System->UpdateBuildingListener(FVector(3000, 0, 0));
        TestTrue(TEXT("Room re-registration restores membership"), System->IsListenerInsideBuilding(Other));
    }
    else AddInfo(TEXT("Native Room location query not run: -nosound disables Wwise; authoring/index checks only."));
    System->UnregisterSource(Other); System->UpdateBuildingListener(FVector(3000, 0, 0));
    TestFalse(TEXT("Unregister removes membership"), System->IsListenerInsideBuilding(Other));
    System->UnregisterSource(Building);
    TestTrue(TEXT("Index cleanup"), System->RoomBuildingsWeakPtrs.IsEmpty());
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    const auto SelectRoom = [&Manager](AAkSpatialAudioVolume* Room)
    {
        for (const auto& Entry : Manager->Controller->State.Entries)
            if (Entry->ActorWeakPtr.Get() == Room) Manager->List->SetItemSelection(Entry, true);
    };
    SelectRoom(First); SelectRoom(Second); SelectRoom(Third);
    TestTrue(TEXT("Member selection immediately shows both group names"), Manager->Controller->State.SharedRoomNotice.Contains(TEXT("House")) && Manager->Controller->State.SharedRoomNotice.Contains(TEXT("Lobby")));
    Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
    TestEqual(TEXT("Room entry keeps three Room targets, not two building records"), Manager->Details->GetSelectedObjects().Num(), 3);
    for (const auto& Object : Manager->Details->GetSelectedObjects())
        TestTrue(TEXT("Room edit never targets shared record"), Object.IsValid() && Object->IsA<AAkSpatialAudioVolume>());
    Manager->Controller->EditBuildingSelection();
    TestEqual(TEXT("Dedicated building editor guard also retains Room targets"), Manager->Details->GetSelectedObjects().Num(), 3);
    TestEqual(TEXT("Room entry cannot change first shared Event"), Building->BuildingEvent.Get(), Event);
    TestEqual(TEXT("Room entry cannot change second shared Event"), Other->BuildingEvent.Get(), Event);
    Manager->List->ClearSelection(); SelectRoom(Third);
    Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
    TestTrue(TEXT("Single member keeps its own Room Details"), Manager->Details->GetSelectedObjects().Num() == 1 && Manager->Details->GetSelectedObjects()[0].Get() == Third);
    TestTrue(TEXT("Room native properties remain editable"), Manager->Details->IsPropertyEditingEnabled());
    auto FindSharedEvent = [&Manager](AAkSpatialAudioVolume* Room)
    {
        for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->ActorWeakPtr.Get() == Room) return Entry->Cells.FindRef("Event");
        return FString(TEXT("Missing Room row"));
    };
    const auto NativeEvent = Third->Room->AkAudioEvent;
    Third->Room->AkAudioEvent = nullptr;
    Manager->Controller->Refresh();
    TestEqual(TEXT("Room metadata clear is independent of building Event"), FindSharedEvent(Third), FString(TEXT("[建筑] ")) + FSoftObjectPath(Event).GetAssetName());
    TestEqual(TEXT("Building Event unchanged by Room metadata"), Other->BuildingEvent.Get(), Event);
    Third->Room->AkAudioEvent = NativeEvent;
    Manager->List->ClearSelection();
    for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->SourceWeakPtr.Get() == Other) Manager->List->SetItemSelection(Entry, true);
    TestFalse(TEXT("Audio-object building row Details are read-only"), Manager->Details->IsPropertyEditingEnabled());
    Manager->Controller->SetPage(1);
    TestTrue(TEXT("Building read-only scope does not lock regional budget editing"), Manager->Details->IsPropertyEditingEnabled());
    Manager->Controller->SetPage(0);
    for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->SourceWeakPtr.Get() == Other) Manager->List->SetItemSelection(Entry, true);
    Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
    TestEqual(TEXT("Audio-object building entry cannot change Event"), Other->BuildingEvent.Get(), Event);
    Manager->Controller->DeleteBuildingSelection();
    TestEqual(TEXT("Audio-object entry cannot delete shared records"), Buildings().Num(), 2);
    Manager->Controller->SetPage(3);
    Manager->Controller->OpenSourceDialog(EAudioSourceAction::Edit);
    TestTrue(TEXT("Dedicated building page permits property edits"), Manager->Details->IsPropertyEditingEnabled());
    TestTrue(TEXT("Dedicated building edit targets actual record"), Manager->Details->GetSelectedObjects().Num() == 1 && Manager->Details->GetSelectedObjects()[0].Get() == Other);
    FWandererAudioResourceGroup City; City.Name = TEXT("City"); Budget->ResourceGroups.Add(City);
    Manager->Controller->ApplyBuildingResourceGroup(TEXT("City"));
    TestEqual(TEXT("Building resource selector writes existing group field"), Other->ResourceGroup, FName(TEXT("City")));
    TestTrue(TEXT("Resource selector caption refreshed"), Manager->Controller->State.SelectedBuildingResourceGroup.ToString().Contains(TEXT("City")));
    TestTrue(TEXT("Unselected building retains independent group"), Building->ResourceGroup.IsNone());
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestTrue(TEXT("Building resource selector Undo"), Other->ResourceGroup.IsNone());
    const int32 MemberCount = Other->MemberRooms.Num();
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Clear shared building event test")));
        Other->Modify(); Other->BuildingEvent.Reset(); Other->PostEditChange();
    }
    Manager->Controller->SetPage(0); Manager->List->ClearSelection(); SelectRoom(Third);
    TestTrue(TEXT("Shared clear readback has no old fallback Event"), FindSharedEvent(Third).IsEmpty());
    TestEqual(TEXT("Clearing Event preserves membership"), Other->MemberRooms.Num(), MemberCount);
    TestTrue(TEXT("Missing shared Event reported invalid"), Other->ValidateConfiguration().Contains(TEXT("Play Event")));
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Shared Event clear Undo restores row"), FindSharedEvent(Third), FString(TEXT("[建筑] ")) + FSoftObjectPath(Event).GetAssetName());
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestTrue(TEXT("Shared Event clear Redo updates row"), FindSharedEvent(Third).IsEmpty());
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    Manager->Controller->SetPage(3);
    TestEqual(TEXT("Building page has two records"), Manager->Controller->State.Entries.Num(), 2);
    for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->SourceWeakPtr.Get() == Building) Manager->List->SetItemSelection(Entry, true);
    Manager->Controller->DeleteBuildingSelection();
    TestEqual(TEXT("Delete only selected record"), Buildings().Num(), 1);
    TestTrue(TEXT("Rooms retained"), IsValid(First) && IsValid(Second));
    TestEqual(TEXT("Actor count still unchanged"), CountActors(), Before);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Delete Undo restores record"), Buildings().Num(), 2);
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestEqual(TEXT("Delete Redo removes record"), Buildings().Num(), 1);
    TestEqual(TEXT("Unselected Event intact"), Other->BuildingEvent.Get(), Event);
    Manager.Reset();
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAdditiveAuthoringTest, "Wanderer.Audio.Manager.RoomAdditive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomAdditiveAuthoringTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->SpawnActor<AWandererWorldAudioBudget>();
    auto* CommonEvent = NewObject<UAkAudioEvent>();
    auto* SpecialEvent = NewObject<UAkAudioEvent>();
    const auto MakeRoom = [World, CommonEvent](float X)
    {
        auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(World, AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform(FVector(X, 0, 0))));
        Room->Room->bEnable = true; Room->Room->AutoPost = false; Room->Room->AkAudioEvent = CommonEvent;
        return Room;
    };
    auto* First = MakeRoom(0);
    auto* Second = MakeRoom(1000);
    auto* Third = MakeRoom(2000);
    auto* Building = UAudioAuthoringLibrary::CreateBuildingAmbience({First, Second}, TEXT("Shared"), CommonEvent);
    if (!TestNotNull(TEXT("Building with empty Room layers"), Building)) return false;
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    Manager->Controller->State.RoomProfileTablePath = FSoftObjectPath(Table);
    const auto SelectRoom = [&Manager](AAkSpatialAudioVolume* Room)
    {
        Manager->List->ClearSelection();
        for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->ActorWeakPtr.Get() == Room) Manager->List->SetItemSelection(Entry, true);
    };
    const auto RoomValue = [&Manager](AAkSpatialAudioVolume* Room, FName Column)
    {
        for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->ActorWeakPtr.Get() == Room) return Entry->Cells.FindRef(Column);
        return FString(TEXT("Missing Room"));
    };
    SelectRoom(First);
    TestTrue(TEXT("Shared member picker starts empty despite native metadata"), Manager->Controller->State.RoomEventPath.IsNull());
    const FString CommonLabel = TEXT("[建筑] ") + FSoftObjectPath(CommonEvent).GetAssetName();
    TestEqual(TEXT("Combined column labels building origin"), RoomValue(First, "Event"), CommonLabel);
    TestNull(TEXT("Joining creates no extra source"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    Manager->Controller->State.RoomEventPath = FSoftObjectPath(SpecialEvent); Manager->Controller->ApplyRoomSelection();
    auto* Layer = First->FindComponentByClass<UWandererAudioSourceComponent>();
    if (!TestNotNull(TEXT("Explicit Event creates Room layer"), Layer)) return false;
    TestEqual(TEXT("Own Event is special Event"), Layer->GetProfile()->PlayEvent.Get(), SpecialEvent);
    TestEqual(TEXT("Building Event untouched"), Building->BuildingEvent.Get(), CommonEvent);
    TestEqual(TEXT("Native Room metadata untouched"), First->Room->AkAudioEvent.Get(), CommonEvent);
    TestEqual(TEXT("Both layers validate"), Building->ValidateConfiguration(), FString());
    TestEqual(TEXT("Room lookup excludes building source"), FAudioSourceAuthoringOperation::GatherSources(World, {First}).Num(), 1);
    TestEqual(TEXT("Combined column keeps both source identities"), RoomValue(First, "Event"), CommonLabel + TEXT("\n[自身] ") + FSoftObjectPath(SpecialEvent).GetAssetName());
    auto* System = NewObject<UWandererAudioSubsystem>(World);
    System->RegisterSource(Building); System->RegisterSource(Layer);
    TestEqual(TEXT("Building-first registration permits layer"), System->GetRegisteredCount(), 2);
    System->UnregisterSource(Building); System->UnregisterSource(Layer);
    System->RegisterSource(Layer); System->RegisterSource(Building);
    TestEqual(TEXT("Layer-first registration permits building"), System->GetRegisteredCount(), 2);
    System->UnregisterSource(Layer); System->UnregisterSource(Building);
    Manager->Controller->State.RoomEventPath.Reset(); Manager->Controller->ApplyRoomSelection();
    TestNull(TEXT("Clearing removes only own layer"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    TestEqual(TEXT("Cleared own layer still shows labelled building Event"), RoomValue(First, "Event"), CommonLabel);
    TestEqual(TEXT("Shared Event survives clear"), Building->BuildingEvent.Get(), CommonEvent);
    GEditor->UndoTransaction(); Manager->Controller->Refresh();
    TestNotNull(TEXT("Clear Undo restores own layer"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    GEditor->RedoTransaction(); Manager->Controller->Refresh();
    TestNull(TEXT("Clear Redo removes own layer"), First->FindComponentByClass<UWandererAudioSourceComponent>());
    FWandererWorldAudioProfileRow Row; Row.Mode = EWandererWorldAudioMode::Room; Row.PlayEvent = SpecialEvent;
    Table->AddRow(TEXT("Special"), Row);
    auto* ExistingLayer = UAudioAuthoringLibrary::AttachRoomAudioComponent(Third, nullptr);
    ExistingLayer->Profile.DataTable = Table; ExistingLayer->Profile.RowName = TEXT("Special");
    TestNotNull(TEXT("Joining with intentional existing layer is allowed"), UAudioAuthoringLibrary::CreateBuildingAmbience({Third}, TEXT("Other"), CommonEvent));
    TestEqual(TEXT("Joining preserves explicit Room Event"), ExistingLayer->GetProfile()->PlayEvent.Get(), SpecialEvent);
    Manager.Reset(); FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
#endif
