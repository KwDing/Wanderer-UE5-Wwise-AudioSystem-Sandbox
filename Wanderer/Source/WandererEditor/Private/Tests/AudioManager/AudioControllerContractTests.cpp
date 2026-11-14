#include "Features/AudioManager/AudioManagerInteraction.h"
#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    class FTestAudioInteraction : public IAudioManagerInteraction
    {
      public:
        TWeakPtr<FAudioManagerController> ControllerWeakPtr;
        TArray<FAudioManagerEntryPtr> Selection;
        TSharedPtr<FAudioSourceDialogDraft> Draft;
        bool bStopOnRefresh = false;
        bool bStopOnDetails = false;

        virtual void RefreshTable(const TArray<FAudioManagerEntryPtr>& InSelection) override
        {
            Selection = InSelection;
            if (bStopOnRefresh)
            {
                if (const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin())
                {
                    Controller->Stop();
                }
            }
        }
        virtual void SetDetailsObjects(const TArray<UObject*>& InObjects) override
        {
            if (bStopOnDetails)
            {
                bStopOnDetails = false;
                if (const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin())
                {
                    Controller->Stop();
                }
            }
        }
        virtual void RebuildColumns() override
        {
        }
        virtual bool IsGroupPickerOpen() const override
        {
            return false;
        }
        virtual void RefreshGroupOptions() override
        {
        }
        virtual void RefreshRoomOptions() override
        {
        }
        virtual void ShowSourceDialog(TSharedRef<FAudioSourceDialogDraft> InDraft) override
        {
            Draft = InDraft;
        }
        virtual void ShowBuildingDialog(TSharedRef<FAudioBuildingDraft> InDraft) override
        {
        }
        virtual bool ConfirmRoomDeletion(int32 InCount) override
        {
            return false;
        }
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioControllerContractTest,
    "Wanderer.Audio.Manager.ControllerContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioControllerContractTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AActor* Actor = World->SpawnActor<AWandererAudioSplineEmitter>();
    const TSharedRef<FAudioManagerController> Controller = MakeShared<FAudioManagerController>();
    const TSharedRef<FTestAudioInteraction> View = MakeShared<FTestAudioInteraction>();
    View->ControllerWeakPtr = Controller;
    Controller->Start(View);
    const FAudioManagerEntryPtr* Found = Controller->State.Entries.FindByPredicate(
        [Actor](const FAudioManagerEntryPtr& InEntry)
        {
            return InEntry->ActorWeakPtr.Get() == Actor;
        });
    if (!TestNotNull(TEXT("Headless query finds source"), Found))
    {
        return false;
    }
    const FAudioManagerEntryPtr Entry = *Found;
    Controller->SelectionChanged({Entry});
    Controller->Refresh();
    TestTrue(TEXT("Refresh preserves row identity"), Controller->State.Entries.Contains(Entry));
    TestTrue(TEXT("Selection model restored to view"), View->Selection.Contains(Entry));
    AWandererWorldAudioBudget* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup FirstGroup;
    FirstGroup.Name = TEXT("First");
    FWandererAudioResourceGroup SecondGroup;
    SecondGroup.Name = TEXT("Second");
    Budget->ResourceGroups = {FirstGroup, SecondGroup};
    UWandererAudioSourceComponent* Audio =
        Actor->FindComponentByClass<UWandererAudioSourceComponent>();
    Audio->ResourceGroup = FirstGroup.Name;
    Controller->SetPage(1);
    if (!TestEqual(TEXT("Two configured groups listed"), Controller->State.Entries.Num(), 2))
    {
        return false;
    }
    const FAudioManagerEntryPtr FirstRow = Controller->State.Entries[0];
    Controller->SelectionChanged({FirstRow});
    Budget->ResourceGroups.Swap(0, 1);
    Controller->QueueDetailsRefresh();
    Controller->TickManager(0);
    TestTrue(TEXT("Selection follows the group across array reordering"),
             View->Selection.Contains(FirstRow));
    TestEqual(TEXT("Consumer count follows the group"),
              FirstRow->Cells.FindRef("Sources"), FString(TEXT("1")));
    Budget->ResourceGroups.RemoveAt(1);
    Controller->QueueDetailsRefresh();
    Controller->TickManager(0);
    TestTrue(TEXT("Deleting a group invalidates its selection"), View->Selection.IsEmpty());
    TestTrue(TEXT("Deleted group's consumers are surfaced in resource-page diagnostics"),
             Controller->State.ResourceConfigurationNotice.Contains(TEXT("First")));
    Controller->SetPage(0);
    Controller->SelectionChanged(Controller->State.Entries);
    Controller->OpenSourceDialog(EAudioSourceAction::Create);
    if (!TestTrue(TEXT("Draft produced without Slate"), View->Draft.IsValid()))
    {
        return false;
    }
    Controller->Stop();
    TestFalse(TEXT("Cached draft cannot commit after stop"),
        Controller->ApplySourceDraft(View->Draft.ToSharedRef()));
    TestTrue(TEXT("Stop empties table model"), Controller->State.Entries.IsEmpty());

    const TSharedRef<FAudioManagerController> Reentrant = MakeShared<FAudioManagerController>();
    const TSharedRef<FTestAudioInteraction> ClosingView = MakeShared<FTestAudioInteraction>();
    ClosingView->ControllerWeakPtr = Reentrant;
    ClosingView->bStopOnRefresh = true;
    Reentrant->Start(ClosingView);
    TestTrue(TEXT("View can stop during initial refresh"), Reentrant->bStopped);
    TestFalse(TEXT("Start cannot register ticker after reentrant stop"),
        Reentrant->RefreshTicker.IsValid());
    TestTrue(TEXT("Refresh cannot refill stopped state"), Reentrant->State.Entries.IsEmpty());
    TestTrue(TEXT("Selection released on stop"), Reentrant->SelectedEntries.IsEmpty());
    AActor* BuildingOwner = World->SpawnActor<AActor>();
    UWandererBuildingAmbienceComponent* Building =
        NewObject<UWandererBuildingAmbienceComponent>(BuildingOwner);
    BuildingOwner->AddInstanceComponent(Building);
    Building->RegisterComponent();
    const TSharedRef<FAudioManagerController> Deleting = MakeShared<FAudioManagerController>();
    const TSharedRef<FTestAudioInteraction> DeletingView = MakeShared<FTestAudioInteraction>();
    DeletingView->ControllerWeakPtr = Deleting;
    Deleting->Start(DeletingView);
    Deleting->SetPage(3);
    const FAudioManagerEntryPtr BuildingEntry = MakeShared<FAudioManagerEntry>();
    BuildingEntry->ActorWeakPtr = BuildingOwner;
    BuildingEntry->SourceWeakPtr = Building;
    Deleting->State.Entries.Add(BuildingEntry);
    Deleting->SelectionChanged({BuildingEntry});
    DeletingView->bStopOnDetails = true;
    Deleting->DeleteBuildingSelection();
    TestTrue(TEXT("Details callback stops deletion"), Deleting->bStopped);
    TestTrue(TEXT("Building survives stopped deletion"),
        IsValid(Building) && BuildingOwner->GetInstanceComponents().Contains(Building));
    return true;
}
#endif
