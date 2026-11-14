#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "AkAmbientSound.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "AkAudioEvent.h"
#include "Components/SplineComponent.h"
#include "Components/LineBatchComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "SceneManagement.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "PropertyEditorDelegates.h"
#include "AkAcousticPortal.h"
#include "AkComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "InteriorFitArea.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplineCharacterLinkTest, "Wanderer.Audio.Manager.SplineCharacterLink", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSplineCharacterLinkTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Editor world"), World)) return false;
    ULineBatchComponent* Batch = World->PersistentLineBatcher.Get();
    if (!TestNotNull(TEXT("Line batch"), Batch)) return false;
    APawn* Pawn = World->SpawnActor<APawn>(FVector(120, 340, 560), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Temporary pawn"), Pawn)) return false;
    const FVector Position(-100, -200, -300);
    Batch->ClearBatch(AudioVisualizationSettings::DefaultDrawBatchID);
    FAudioVisualizationController::DrawSplinePlaybackLink(*Batch, Position, Pawn);
    TestTrue(TEXT("Emitter link ends at pawn Actor origin"), Batch->BatchedLines.ContainsByPredicate([&Position, Pawn](const FBatchedLine& Line)
    {
        return Line.BatchID == AudioVisualizationSettings::DefaultDrawBatchID && Line.Start.Equals(Position) && Line.End.Equals(Pawn->GetActorLocation());
    }));
    TestTrue(TEXT("Pawn marker is yellow"), Batch->BatchedPoints.ContainsByPredicate([Pawn](const FBatchedPoint& Point)
    {
        return Point.BatchID == AudioVisualizationSettings::DefaultDrawBatchID && Point.Position.Equals(Pawn->GetActorLocation()) && Point.Color == FLinearColor::Yellow;
    }));
    Batch->ClearBatch(AudioVisualizationSettings::DefaultDrawBatchID);
    FAudioVisualizationController::DrawSplinePlaybackLink(*Batch, Position, nullptr);
    TestFalse(TEXT("No pawn means no misleading link"), Batch->BatchedLines.ContainsByPredicate([](const FBatchedLine& Line) { return Line.BatchID == AudioVisualizationSettings::DefaultDrawBatchID; }));
    Batch->ClearBatch(AudioVisualizationSettings::DefaultDrawBatchID);
    TestFalse(TEXT("Clear removes owned points"), Batch->BatchedPoints.ContainsByPredicate([](const FBatchedPoint& Point) { return Point.BatchID == AudioVisualizationSettings::DefaultDrawBatchID; }));
    auto* DebugArea = World->SpawnActor<AWandererAudioAreaEmitter>();
    auto* DebugPoint = World->SpawnActor<AWandererWorldAudioEmitter>(FVector(300, 0, 0), FRotator::ZeroRotator);
    auto* Shape = World->SpawnActor<AWandererAudioSplineEmitter>();
    if (!DebugArea || !DebugPoint || !Shape)
    {
        if (DebugArea) DebugArea->Destroy();
        if (DebugPoint) DebugPoint->Destroy();
        if (Shape) Shape->Destroy();
        Pawn->Destroy();
        AddError(TEXT("Cannot spawn relation fixtures")); return false;
    }
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    const auto Configure = [Table](UWandererAudioSourceComponent* Source, EWandererWorldAudioMode Mode, FName Name)
    {
        FWandererWorldAudioProfileRow Row;
        Row.Mode = Mode;
        Table->AddRow(Name, Row);
        Source->Profile.DataTable = Table;
        Source->Profile.RowName = Name;
    };
    Configure(DebugPoint->AudioSource, EWandererWorldAudioMode::Point, TEXT("Snow"));
    Configure(DebugArea->AudioSource, EWandererWorldAudioMode::Area, TEXT("Grass"));
    Configure(Shape->AudioSource, EWandererWorldAudioMode::Spline, TEXT("Lake"));
    DebugArea->SetSurfaceCells({FVector(1200, 0, 0)});
    const FVector Center = Shape->GetActorLocation();
    TestTrue(TEXT("Lake interior included"), FAudioVisualizationController::GetPlaybackDistance(*Shape->AudioSource, Center, 20) <= 20);
    TestTrue(TEXT("Within20m included"), FAudioVisualizationController::GetPlaybackDistance(*Shape->AudioSource, Center + FVector(2299, 0, 0), 20) <= 20);
    TestTrue(TEXT("Beyond20m excluded"), FAudioVisualizationController::GetPlaybackDistance(*Shape->AudioSource, Center + FVector(2301, 0, 0), 20) > 20);
    const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources = {DebugPoint->AudioSource.Get(), DebugArea->AudioSource.Get(), Shape->AudioSource.Get()};
    const auto Relations = FAudioVisualizationController::GatherPlaybackRelations(Sources, *Pawn, 20);
    TestEqual(TEXT("Point Snow, Area Grass and Spline Lake all included"), Relations.Num(), 3);
    for (const auto& Relation : Relations) TestFalse(TEXT("Inactive source never gets a fabricated posted point"), Relation.bPosted);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const FVector EmitterPosition(100 + Index * 200, 700, 50);
        const FLinearColor Color = FLinearColor::MakeFromHSV8(uint8(Index * 83), 210, 255);
        FAudioVisualizationController::DrawSplinePlaybackLink(*Batch, EmitterPosition, Pawn, Color);
        TestTrue(TEXT("Each source line starts at its own emitter, not the foot cell"), Batch->BatchedLines.ContainsByPredicate([EmitterPosition, Pawn, Color](const FBatchedLine& Line)
        { return Line.BatchID == AudioVisualizationSettings::DefaultDrawBatchID && Line.Start.Equals(EmitterPosition) && Line.End.Equals(Pawn->GetActorLocation()) && Line.Color == Color; }));
    }
    Batch->ClearBatch(AudioVisualizationSettings::DefaultDrawBatchID);
    DebugArea->Destroy();
    DebugPoint->Destroy();
    Shape->Destroy();
    Pawn->Destroy();
    return true;
}
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioManagerTableTest, "Wanderer.Audio.Manager.Table", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioManagerTableTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Load saved fixture map"), FEditorFileUtils::LoadMap(FPaths::ProjectContentDir() / TEXT("Maps/L_AudioSystemDemo.umap"), false, false))) return false;
    UDataTable* PolicyTable = NewObject<UDataTable>();
    PolicyTable->RowStruct = FWandererAudioResourcePolicyRow::StaticStruct();
    FWandererAudioResourcePolicyRow PolicyRow;
    PolicyRow.MaxEvents = 2; PolicyRow.LoadDistanceM = 123;
    PolicyTable->AddRow(TEXT("Test"), PolicyRow);
    FWandererAudioResourceGroup Group;
    Group.Policy.DataTable = PolicyTable; Group.Policy.RowName = TEXT("Test");
    FString Error;
    TestTrue(TEXT("Policy resolves"), Group.ApplyPolicy(Error));
    TestEqual(TEXT("Policy budget"), Group.MaxEvents, 2);
    TestEqual(TEXT("Policy distance"), Group.LoadDistanceM, 123.f);
    Group.Policy.RowName = TEXT("Missing");
    TestFalse(TEXT("Missing policy is rejected"), Group.ApplyPolicy(Error));
    auto Manager = SNew(SAudioManagerPanel);
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("World Audio Manager Verification"))).ClientSize(FVector2D(1500, 800))[Manager];
    FSlateApplication::Get().AddWindow(Window);
    Manager->Controller->ShowAllColumns(0);
    TestTrue(TEXT("Native multi-column headers"), Manager->Header->GetColumns().Num() > 8);
    TestFalse(TEXT("Room event controls hidden by default"), Manager->Controller->State.bShowRoomActions);
    Manager->Controller->State.TypeFilter = TEXT("Room 空间"); Manager->Controller->Refresh();
    TestTrue(TEXT("Rooms available"), !Manager->Controller->State.Entries.IsEmpty());
    for (const auto& Entry : Manager->Controller->State.Entries) TestEqual(TEXT("Room filter"), Entry->Kind, FString(TEXT("Room 空间")));
    const auto* Diner = Manager->Controller->State.Entries.FindByPredicate([](const auto& Entry) { return Entry->Cells.FindRef("Name") == TEXT("ACOUSTIC_ROOM__Diner"); });
    TestNotNull(TEXT("Diner Room exists"), Diner);
    if (Diner)
    {
        TestTrue(TEXT("Diner owns managed component"), (*Diner)->SourceWeakPtr.IsValid() && (*Diner)->SourceWeakPtr->GetOwner() == (*Diner)->ActorWeakPtr.Get());
        TestFalse(TEXT("Diner shows event"), (*Diner)->Cells.FindRef("Event").IsEmpty());
        TestTrue(TEXT("Diner binding searchable"), (*Diner)->Description.Contains(TEXT("ACOUSTIC_ROOM__Diner")));
        const auto Assets = Manager->Controller->GetOpenAssets(*Diner);
        TestEqual(TEXT("Open Room resolves exactly one DT"), Assets.Num(), 1);
        Manager->List->SetSelection(*Diner);
        Manager->Controller->OpenSelection();
        auto* Table = (*Diner)->SourceWeakPtr->Profile.DataTable.Get();
        auto* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        TestNotNull(TEXT("Unified Open actually opens Room DT editor"), AssetEditors->FindEditorForAsset(const_cast<UDataTable*>(Table), false));
        AssetEditors->CloseAllEditorsForAsset(const_cast<UDataTable*>(Table));

    }
    Manager->Controller->State.Filter = TEXT("ACOUSTIC_ROOM__Diner"); Manager->Controller->Refresh();
    TestTrue(TEXT("Emitter search retains associated Room"), !Manager->Controller->State.Entries.IsEmpty());
    Manager->Controller->State.Filter.Reset();
    Manager->Controller->State.TypeFilter = TEXT("Portal 门洞"); Manager->Controller->Refresh();
    TestTrue(TEXT("Portals discoverable"), !Manager->Controller->State.Entries.IsEmpty());
    Manager->Controller->State.TypeFilter = TEXT("全部类型"); Manager->Controller->State.GroupFilter = TEXT("Tunnel"); Manager->Controller->Refresh();
    TestTrue(TEXT("Group filter has members"), !Manager->Controller->State.Entries.IsEmpty());
    for (const auto& Entry : Manager->Controller->State.Entries) TestEqual(TEXT("Group filter"), Entry->Cells.FindRef("Group"), FString(TEXT("Tunnel")));
    Manager->Controller->State.GroupFilter = TEXT("全部资源组"); Manager->Controller->Refresh();
    if (!Manager->Controller->State.Entries.IsEmpty()) Manager->List->SetItemSelection(Manager->Controller->State.Entries[0], true);
    Manager->Controller->State.bShowRoomActions = true;
    Manager->Controller->State.Filter = TEXT("__no_matching_audio__"); Manager->Controller->Refresh();
    TestFalse(TEXT("Cleared selection collapses Room event controls"), Manager->Controller->State.bShowRoomActions);
    TestEqual(TEXT("Empty filter clears details"), Manager->Details->GetSelectedObjects().Num(), 0);
    Manager->Controller->State.Filter.Reset(); Manager->Controller->SetPage(1);
    TestEqual(TEXT("Three resource groups"), Manager->Controller->State.Entries.Num(), 3);
    for (const auto& Entry : Manager->Controller->State.Entries) TestEqual(TEXT("Regional budget Open resolves policy DT"), Manager->Controller->GetOpenAssets(Entry).Num(), 1);
    Manager->Controller->SetPage(2); TestEqual(TEXT("Removed map budget page cannot activate"), Manager->Controller->State.ActivePage, 1);
    Manager->Controller->State.bProjectAssets = true; Manager->Controller->State.ActivePage = 0; Manager->Controller->Refresh();
    TestTrue(TEXT("Project inventory"), !Manager->Controller->State.Entries.IsEmpty());
    for (const auto& Entry : Manager->Controller->State.Entries)
    {
        TestFalse(TEXT("Asset path without actor load"), Entry->AssetPath.IsNull());
        TestTrue(TEXT("Project Open resolves selected asset"), Manager->Controller->GetOpenAssets(Entry).Contains(Entry->AssetPath));
    }
    for (const TCHAR* Kind : {TEXT("DT 资产"), TEXT("Event 资产"), TEXT("地图资产")})
    {
        Manager->Controller->State.TypeFilter = Kind; Manager->Controller->Refresh();
        TestTrue(TEXT("Asset type filter has matching rows"), !Manager->Controller->State.Entries.IsEmpty());
        for (const auto& Entry : Manager->Controller->State.Entries) TestEqual(TEXT("Asset kind matches picker"), Entry->Kind, FString(Kind));
    }
    Manager->Controller->State.TypeFilter = TEXT("全部类型");
    Manager->Controller->State.bProjectAssets = false; Manager->Controller->SetPage(0);
    auto* Area = Manager->Controller->GetObservedWorld()->SpawnActor<AWandererAudioAreaEmitter>();
    AActor* Surface = Manager->Controller->GetObservedWorld()->SpawnActor<AActor>();
    Surface->SetActorLabel(TEXT("GrassSurface"));
    Area->Tags.Add(FName(TEXT("AudioAutoFitInput:") + Surface->GetPathName()));
    SAudioManagerPanel::FEntry Association;
    Association.ActorWeakPtr = Area; Association.SourceWeakPtr = Area->AudioSource;
    Manager->Controller->FillEntry(Association, Manager->Controller->FindBudget(), {});
    TestEqual(TEXT("Second column shows surface label"), Association.Cells.FindRef("Room"), FString(TEXT("GrassSurface")));
    Surface->SetActorLabel(TEXT("RenamedGrass"));
    Manager->Controller->FillEntry(Association, Manager->Controller->FindBudget(), {});
    TestEqual(TEXT("Association follows display rename"), Association.Cells.FindRef("Room"), FString(TEXT("RenamedGrass")));
    Surface->Tags.Add(TEXT("AudioAutoFitPreview"));
    Surface->Tags.Add(FName(TEXT("AudioAutoFitInput:") + Area->GetPathName()));
    Manager->Controller->Refresh();
    TestTrue(TEXT("Preview visible without audio component"), Manager->Controller->State.Entries.ContainsByPredicate([Surface](const auto& Item)
        { return Item->ActorWeakPtr.Get() == Surface && Item->Kind == TEXT("地表预览"); }));
    for (const auto& Item : Manager->Controller->State.Entries) if (Item->ActorWeakPtr.Get() == Surface) Manager->List->SetSelection(Item);
    TestFalse(TEXT("Preview cannot use ordinary source authoring"), Manager->Controller->CanEditRoomSources());
    Manager->List->ClearSelection();
    Surface->Destroy();
    Manager->Controller->FillEntry(Association, Manager->Controller->FindBudget(), {});
    TestTrue(TEXT("Missing association is explicit"), Association.Cells.FindRef("Room").Contains(TEXT("缺失")));
    Area->SetSurfaceCells({ FVector(10000, 20000, 0), FVector(11000, 21000, 0) });
    Area->SetActorTransform(FTransform(FRotator(0, 35, 0), FVector(200, 300, 400), FVector(2, 1, 1)));
    Manager->Controller->Refresh();
    Manager->List->ClearSelection();
    for (const auto& Entry : Manager->Controller->State.Entries) if (Entry->ActorWeakPtr.Get() == Area) Manager->List->SetItemSelection(Entry, true);
    const auto& Viewports = GEditor->GetLevelViewportClients();
    FLevelEditorViewportClient* Viewport = nullptr;
    for (FLevelEditorViewportClient* Candidate : Viewports)
    {
        if (Candidate && !Candidate->IsOrtho())
        {
            Viewport = Candidate;
            break;
        }
    }
    const FVector BeforeFocus = Viewport
        ? Viewport->GetViewTransform().GetLookAt() : FVector::ZeroVector;
    {
        // ACTIVEVIEWPORTONLY targets the active client, not the first client in the array.
        TGuardValue<FLevelEditorViewportClient*> ActiveViewportGuard(
            GCurrentLevelEditingViewportClient, Viewport);
        Manager->Controller->OpenSelection();
    }
    TestTrue(TEXT("Open without asset selects area actor"), Area->IsSelected());
    TestNotNull(TEXT("Perspective editor viewport exists"), Viewport);
    if (Viewport)
    {
        // UE updates the focus pivot now and animates camera location on later frames.
        TestFalse(TEXT("Area focus updates camera target"),
            Viewport->GetViewTransform().GetLookAt().Equals(BeforeFocus));
    }
    GEditor->SelectNone(false, true);
    Area->Destroy(); Manager->Controller->Refresh();
    AActor* DebugActor = Manager->Controller->GetObservedWorld()->SpawnActor<AWandererAudioSplineEmitter>();
    Manager->Controller->Refresh(); Manager->Controller->VisualizationController->SetEnabled(true); Manager->Controller->VisualizationController->DrawNow();
    auto* Batch = Manager->Controller->GetObservedWorld()->PersistentLineBatcher.Get();
    TestTrue(TEXT("Editor spline debug draws outline"), Batch->BatchedLines.ContainsByPredicate([Manager = Manager](const FBatchedLine& Line) { return Line.BatchID == Manager->Controller->VisualizationController->DrawBatchID; }));
    Manager->Controller->VisualizationController->ClearDrawing();
    TestFalse(TEXT("Debug clear owns only its batch"), Batch->BatchedLines.ContainsByPredicate([Manager = Manager](const FBatchedLine& Line) { return Line.BatchID == Manager->Controller->VisualizationController->DrawBatchID; }));
    DebugActor->Destroy(); Manager->Controller->VisualizationController->SetEnabled(false); Manager->Controller->Refresh();
    Window->SlatePrepass();
    TArray<FColor> Pixels; FIntVector Size;
    if (FSlateApplication::Get().TakeScreenshot(Window, Pixels, Size))
    {
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
        FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectDir() / TEXT("../AgentKit/reports/world_audio_toolbar/table.png")));
    }
    Manager->Controller->SetPage(3);
    if (!Manager->Controller->State.Entries.IsEmpty())
    {
        TestTrue(TEXT("Building without DT falls back to members"), Manager->Controller->GetOpenAssets(Manager->Controller->State.Entries[0]).IsEmpty());
        Manager->List->SetItemSelection(Manager->Controller->State.Entries[0], true); Manager->Controller->OpenSelection();
        TestTrue(TEXT("Building Open selects member Rooms"), GEditor->GetSelectedActorCount() > 0);
        Manager->Controller->EditBuildingSelection();
        Window->SlatePrepass();
        if (FSlateApplication::Get().TakeScreenshot(Window, Pixels, Size))
        {
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
            FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectDir() / TEXT("../AgentKit/reports/world_audio_toolbar/buildings.png")));
        }
    }
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return true;
}
#include "AkAudioDevice.h"
#include "Camera/CameraActor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

// Framework owns this synchronous game-thread command/test until queued EndPlay completes.
class FNearbySourceRelationsProbe : public IAutomationLatentCommand
{
public:
    explicit FNearbySourceRelationsProbe(FAutomationTestBase* InTest) : Test(InTest), Deadline(FPlatformTime::Seconds() + 45)
    {
        // Construct and enable before the queued StartPIE command executes.
        Manager = SNew(SAudioManagerPanel);
        Manager->Controller->State.bProjectAssets = true;
        Manager->Controller->VisualizationController->SetEnabled(true);
        Manager->SetVisibility(EVisibility::Collapsed);
        Manager->Controller->Refresh();
    }
    virtual bool Update() override
    {
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
        APawn* Pawn = Player ? Player->GetPawn() : nullptr;
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Nearby relation PIE probe timed out")); return true; }
        if (!Pawn) return false;
        Pawn->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
        if (!bSetup)
        {
            const FVector CameraLocation(-1000, -1400, 1300);
            auto* Camera = World->SpawnActor<ACameraActor>(CameraLocation, (FVector(800, 0, 0) - CameraLocation).Rotation());
            Player->SetViewTarget(Camera);
            Player->SetAudioListenerOverride(Pawn->GetRootComponent(), FVector(0, 0, 80), FRotator::ZeroRotator);
            bSetup = true;
            Manager->Controller->State.Filter = TEXT("__matches_no_source__");
            Manager->Controller->VisualizationController->SetEnabled(true);
            Manager->Controller->Refresh();
        }
        const auto Relations = FAudioVisualizationController::GatherPlaybackRelations(Manager->Controller->VisualizationController->SourcesWeakPtr, *Pawn, 20);
        int32 Posted = 0;
        for (const auto& Relation : Relations) Posted += Relation.bPosted;
        if (Posted != 3) return false;
        // Observe the actual ticker after the editor-to-PIE transition; do not draw manually.
        if (Manager->Controller->VisualizationController->VisualizationWorldWeakPtr.Get() != World || World->PersistentLineBatcher->BatchedLines.IsEmpty()) return false;
        // The first batch can precede our pawn teleport/listener override. Wait for a fresh draw.
        for (const auto& Relation : Relations)
        {
            const FVector Position = Relation.Position;
            if (!World->PersistentLineBatcher->BatchedLines.ContainsByPredicate([Position, Pawn, Manager = Manager](const FBatchedLine& Line)
                { return Line.BatchID == Manager->Controller->VisualizationController->DrawBatchID && Line.Start.Equals(Position) && Line.End.Equals(Pawn->GetActorLocation()); })) return false;
        }
        if (ScreenshotAt < 0)
        {
            Test->TestTrue(TEXT("PIE debug runs while browsing project assets"), Manager->Controller->State.bProjectAssets);
            auto* Batch = World->PersistentLineBatcher.Get();
            for (const auto& Relation : Relations)
            {
                const FVector Position = Relation.Position;
                Test->TestTrue(TEXT("Posted emitter remains separate from pawn"), FVector::Distance(Position, Pawn->GetActorLocation()) > 100);
                Test->TestTrue(TEXT("Each actual Point/Area/Spline has its own pawn link despite empty filter"), Batch->BatchedLines.ContainsByPredicate([Position, Pawn, Manager = Manager](const FBatchedLine& Line)
                { return Line.BatchID == Manager->Controller->VisualizationController->DrawBatchID && Line.Start.Equals(Position) && Line.End.Equals(Pawn->GetActorLocation()); }));
            }
            Test->TestTrue(TEXT("All source identities visible in debug legend"), Manager->Controller->VisualizationController->GetSummary().Contains(TEXT("Snow_Point")) && Manager->Controller->VisualizationController->GetSummary().Contains(TEXT("Grass_Area")) && Manager->Controller->VisualizationController->GetSummary().Contains(TEXT("Lake_Spline")));
            ScreenshotAt = World->GetTimeSeconds();
        }
        if (!bScreenshotRequested && World->GetTimeSeconds() - ScreenshotAt > .25)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../AgentKit/reports/nearby_source_relations/pie.png")), false, false);
            bScreenshotRequested = true;
        }
        if (World->GetTimeSeconds() - ScreenshotAt < .7) return false;
        Manager->Controller->VisualizationController->SetEnabled(false);
        Manager->Controller->VisualizationController->ClearDrawing();
        Test->TestFalse(TEXT("Disabling clears only the relation line batch"), World->PersistentLineBatcher->BatchedLines.ContainsByPredicate([Manager = Manager](const FBatchedLine& Line) { return Line.BatchID == Manager->Controller->VisualizationController->DrawBatchID; }));
        return true;
    }
private:
    FAutomationTestBase* Test;
    double Deadline;
    float ScreenshotAt = -1;
    bool bScreenshotRequested = false;
    bool bSetup = false;
    TSharedPtr<SAudioManagerPanel> Manager;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNearbySourceRelationsPIETest, "Wanderer.Audio.Manager.NearbyRelationsPIE", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNearbySourceRelationsPIETest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("NearbySourceRelationsMutedProbe")))
    { AddError(TEXT("Requires isolated -NearbySourceRelationsMutedProbe process")); return false; }
    auto* SoundEngine = IWwiseSoundEngineAPI::Get();
    if (!SoundEngine || !FAkAudioDevice::IsInitialized() || SoundEngine->SetOutputVolume(0, 0) != AK_Success)
    { AddError(TEXT("Native mute not verified; no playback started")); return false; }
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    World->SpawnActor<AWandererWorldAudioBudget>();
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    const auto Configure = [Table](UWandererAudioSourceComponent* Source, FName Name, EWandererWorldAudioMode Mode, const TCHAR* EventPath)
    {
        FWandererWorldAudioProfileRow Row;
        Row.Mode = Mode;
        Row.PlayEvent = FSoftObjectPath(EventPath);
        Table->AddRow(Name, Row);
        Source->Profile.DataTable = Table;
        Source->Profile.RowName = Name;
        Source->ResourceGroup = NAME_None;
        Source->GetOwner()->SetActorLabel(Name.ToString());
    };
    auto* Snow = World->SpawnActor<AWandererWorldAudioEmitter>(FVector(800, 600, 0), FRotator::ZeroRotator);
    auto* Grass = World->SpawnActor<AWandererAudioAreaEmitter>();
    Grass->SetSurfaceCells({FVector(1200, -600, 0)});
    auto* Lake = World->SpawnActor<AWandererAudioSplineEmitter>(FVector(1800, 700, 0), FRotator::ZeroRotator);
    Configure(Snow->AudioSource, TEXT("Snow_Point"), EWandererWorldAudioMode::Point, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_snow.Play_Amb_snow"));
    Configure(Grass->AudioSource, TEXT("Grass_Area"), EWandererWorldAudioMode::Area, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_grass_day.Play_Amb_grass_day"));
    Configure(Lake->AudioSource, TEXT("Lake_Spline"), EWandererWorldAudioMode::Spline, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_lake.Play_Amb_lake"));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FNearbySourceRelationsProbe(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif

