#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioHiddenVisualizationTest,
    "Wanderer.Audio.Manager.HiddenVisualization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioHiddenVisualizationTest::RunTest(const FString& Parameters)
{
    FAudioVisualizationController SettingsProbe;
    TestEqual(TEXT("Default visualization range stays 20m"), SettingsProbe.GetRangeM(), 20.0f);
    SettingsProbe.SetRangeM(-1.0f);
    TestEqual(TEXT("Range lower bound"), SettingsProbe.GetRangeM(), 1.0f);
    SettingsProbe.SetRangeM(2000.0f);
    TestEqual(TEXT("Range upper bound"), SettingsProbe.GetRangeM(), 1000.0f);
    TestEqual(TEXT("Relation budget remains 16"), AudioVisualizationSettings::RelationCountMax, 16);
    TestTrue(TEXT("Posted sources retain priority"),
        AudioVisualizationSettings::bPrioritizePostedSources);
    TestEqual(
        TEXT("Draw cadence remains 100ms"), AudioVisualizationSettings::TickIntervalSec, 0.1f);
    TestEqual(TEXT("Summary cadence remains 1s"),
        AudioManagerRefreshSettings::StateRefreshIntervalSec,
        1.0);
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->SpawnActor<AWandererAudioSplineEmitter>();
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    Manager->SetVisibility(EVisibility::Collapsed);
    Manager->Controller->VisualizationController->SetEnabled(true);
    // No Slate Tick and no window attachment: only the actual registered ticker runs.
    FTSTicker::GetCoreTicker().Tick(.11f);
    TestTrue(TEXT("Hidden panel draws"),
        Manager->Controller->VisualizationController->VisualizationWorldWeakPtr.Get() == World);
    ULineBatchComponent* Batch = World->PersistentLineBatcher.Get();
    if (!TestNotNull(TEXT("Line batcher"), Batch))
    {
        return false;
    }
    TestTrue(TEXT("Hidden panel produces geometry"), !Batch->BatchedLines.IsEmpty());
    Manager->Controller->State.bProjectAssets = true;
    Manager->Controller->Refresh();
    FTSTicker::GetCoreTicker().Tick(.11f);
    TestTrue(TEXT("Project asset browsing does not disable visualization"),
        !Batch->BatchedLines.IsEmpty());
    Manager->Controller->State.bProjectAssets = false;
    Manager->Controller->State.ActivePage = 1;
    Manager->Controller->Refresh();
    FTSTicker::GetCoreTicker().Tick(.11f);
    TestTrue(TEXT("Budget page does not disable visualization"), !Batch->BatchedLines.IsEmpty());
    constexpr uint32 ForeignBatchID = 17;
    Batch->DrawLine(FVector::ZeroVector, FVector(100, 0, 0), FLinearColor::Red,
                    SDPG_World, 1.0f, 10.0f, ForeignBatchID);
    Manager->Controller->VisualizationController->SetEnabled(false);
    TestTrue(TEXT("Disabling leaves other draw batches intact"),
        Batch->BatchedLines.ContainsByPredicate([](const FBatchedLine& InLine)
        {
            return InLine.BatchID == ForeignBatchID;
        }));
    Batch->ClearBatch(ForeignBatchID);
    FTSTicker::GetCoreTicker().Tick(.11f);
    TestTrue(TEXT("Option off clears geometry"), Batch->BatchedLines.IsEmpty());
    Manager->Controller->VisualizationController->SetEnabled(true);
    FTSTicker::GetCoreTicker().Tick(.11f);
    TWeakPtr<SAudioManagerPanel> WeakManager = Manager;
    Manager.Reset();
    TestFalse(TEXT("Ticker does not retain panel"), WeakManager.IsValid());
    TestTrue(TEXT("Destruction clears owned geometry"), Batch->BatchedLines.IsEmpty());
    FTSTicker::GetCoreTicker().Tick(.11f);
    TestTrue(TEXT("No callback redraw after destruction"), Batch->BatchedLines.IsEmpty());
    return true;
}
#endif
