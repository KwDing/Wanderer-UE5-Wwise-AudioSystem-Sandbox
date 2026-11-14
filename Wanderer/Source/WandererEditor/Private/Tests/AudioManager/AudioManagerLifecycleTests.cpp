#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/Docking/SDockTab.h"

#include "Actors/Audio/WandererAudioSplineEmitter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioStoppedPanelTest,
    "Wanderer.Audio.Manager.StoppedPanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioStoppedPanelTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->SpawnActor<AWandererAudioSplineEmitter>();
    const TSharedPtr<SDockTab> Tab =
        FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("WandererAudioManager")));
    if (!TestTrue(TEXT("Registered tab opens"), Tab.IsValid()))
    {
        return false;
    }
    const TSharedRef<SAudioManagerPanel> Panel =
        StaticCastSharedRef<SAudioManagerPanel>(Tab->GetContent());
    const TSharedPtr<FAudioVisualizationController> Controller = Panel->Controller->VisualizationController;
    Controller->SetEnabled(true);
    Controller->RefreshSources();
    Controller->DrawNow();
    ULineBatchComponent* Batch = World->PersistentLineBatcher.Get();
    if (!TestNotNull(TEXT("Line batcher"), Batch))
    {
        Tab->RequestCloseTab();
        return false;
    }
    const uint32 BatchID = Controller->DrawBatchID;
    const TSharedRef<FAudioVisualizationController> Other = MakeShared<FAudioVisualizationController>();
    Other->Start();
    Other->SetEnabled(true);
    Other->DrawNow();
    const uint32 OtherBatchID = Other->DrawBatchID;
    TestTrue(TEXT("Controller batches are independent"), BatchID != OtherBatchID);

    // Keep both panel and controller alive: closing the tab must not rely on destruction.
    Tab->RequestCloseTab();
    TestTrue(TEXT("Tab close explicitly stops panel"), Panel->bStopped);
    TestFalse(TEXT("Editing rejected after close"), Panel->Controller->CanEditRoomSources());
    TestFalse(TEXT("Refresh ticker removed"), Panel->Controller->RefreshTicker.IsValid());
    TestFalse(TEXT("Debug ticker removed"), Controller->TickerHandle.IsValid());
    TestTrue(TEXT("Observed sources released"), Controller->SourcesWeakPtr.IsEmpty());
    Controller->Start();
    Controller->SetEnabled(true);
    Controller->DrawNow();
    TestFalse(TEXT("Stopped controller cannot restart"), Controller->IsEnabled());
    TestFalse(TEXT("Cached tick is rejected"), Controller->OnTick(0.1f));
    TestFalse(TEXT("Owned geometry removed"),
        Batch->BatchedLines.ContainsByPredicate(
            [BatchID](const FBatchedLine& InLine)
            {
                return InLine.BatchID == BatchID;
            }));
    TestTrue(TEXT("Other controller geometry survives"),
        Batch->BatchedLines.ContainsByPredicate(
            [OtherBatchID](const FBatchedLine& InLine)
            {
                return InLine.BatchID == OtherBatchID;
            }));
    Other->Stop();
    Panel->Stop();
    TestTrue(TEXT("Repeated stop stays terminal"), Panel->bStopped);
    return true;
}
#endif
