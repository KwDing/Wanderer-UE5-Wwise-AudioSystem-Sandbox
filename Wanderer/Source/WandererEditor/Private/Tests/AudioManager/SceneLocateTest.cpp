#include "Features/AudioManager/Controller/AudioManagerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "AkSpatialAudioVolume.h"
#include "AkAcousticPortal.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Widgets/Views/SListView.h"
#include "Components/BoxComponent.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSceneLocateTest, "Wanderer.Audio.Manager.SceneLocate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioSceneLocateTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Room = World->SpawnActor<AAkSpatialAudioVolume>();
    auto* Portal = World->SpawnActor<AAkAcousticPortal>();
    auto* Source = NewObject<UWandererAudioSourceComponent>(Room);
    Room->AddInstanceComponent(Source);
    Source->RegisterComponent();
    auto* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    Source->Profile.DataTable = Table;
    auto Manager = SNew(SAudioManagerPanel);
    TArray<SAudioManagerPanel::FEntryPtr> Entries;
    for (const auto& Entry : Manager->Controller->State.Entries)
        if (Entry->ActorWeakPtr.Get() == Room || Entry->ActorWeakPtr.Get() == Portal) Entries.Add(Entry);
    TestEqual(TEXT("Room and portal listed"), Entries.Num(), 2);
    Manager->List->ClearSelection();
    for (const auto& Entry : Entries) Manager->List->SetItemSelection(Entry, true);
    Manager->Controller->FocusEntries(Manager->List->GetSelectedItems());
    TestTrue(TEXT("Room with a table is selected"), Room->IsSelected());
    TestTrue(TEXT("Portal is selected alongside room"), Portal->IsSelected());
    TArray<FVector> CameraLocations;
    for (auto* Viewport : GEditor->GetLevelViewportClients()) CameraLocations.Add(Viewport->GetViewLocation());
    // Exercise the PIE guard without starting gameplay or retaining a different world.
    {
        TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, World);
        GEditor->SelectNone(false, true);
        Manager->Controller->FocusEntries(Manager->List->GetSelectedItems());
        TestTrue(TEXT("PIE guard still permits selection"), Room->IsSelected() && Portal->IsSelected());
        int32 Index = 0;
        for (auto* Viewport : GEditor->GetLevelViewportClients())
            TestTrue(TEXT("PIE guard preserves editor camera"), Viewport->GetViewLocation().Equals(CameraLocations[Index++]));
    }
    GEditor->SelectNone(false, true);
    Room->Destroy();
    Portal->Destroy();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioAreaFocusBoundsTest, "Wanderer.Audio.Manager.AreaFocusBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioAreaFocusBoundsTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Area = World->SpawnActor<AWandererAudioAreaEmitter>();
    Area->CellSize = 100;
    Area->SetSurfaceCells({FVector(-1600, -5800, 0), FVector(-1500, -5700, 0)});
    Area->RefreshEditorFocusBounds();
    auto* Bounds = Area->FindComponentByClass<UBoxComponent>();
    if (TestNotNull(TEXT("Native focus primitive exists"), Bounds))
    {
        TestTrue(TEXT("Focus primitive is stripped from cooking"), Bounds->IsEditorOnly());
        TestEqual(TEXT("No collision"), Bounds->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        TestTrue(TEXT("Offset cells enclosed"), Bounds->Bounds.GetBox().IsInside(FVector(-1600, -5800, 0)));
        TestFalse(TEXT("World origin excluded"), Bounds->Bounds.GetBox().IsInside(FVector::ZeroVector));
        Area->SetSurfaceCells({FVector(8000, 9000, 100)});
        Area->RefreshEditorFocusBounds();
        TestTrue(TEXT("Replacement geometry enclosed"), Bounds->Bounds.GetBox().IsInside(FVector(8000, 9000, 100)));
        TestFalse(TEXT("Old geometry excluded"), Bounds->Bounds.GetBox().IsInside(FVector(-1600, -5800, 0)));
        Area->SetSurfaceCells({});
        Area->RefreshEditorFocusBounds();
        TestTrue(TEXT("Empty area has finite fallback"), Bounds->Bounds.BoxExtent.Equals(FVector(50)));
    }
    Area->Destroy();
    return true;
}
#endif

