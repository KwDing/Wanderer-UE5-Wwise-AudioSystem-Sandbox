#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Tests/AutomationEditorCommon.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Features/AudioManager/Authoring/AudioComponentDeletionOperation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioComponentDeletionRecoveryTest,
                                 "Wanderer.Audio.Authoring.ComponentDeletionRecovery",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FAudioComponentDeletionRecoveryTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AWandererWorldAudioBudget* Owner = World->SpawnActor<AWandererWorldAudioBudget>();
    UWandererBuildingAmbienceComponent* First
        = NewObject<UWandererBuildingAmbienceComponent>(Owner, NAME_None, RF_Transactional);
    UWandererBuildingAmbienceComponent* Second
        = NewObject<UWandererBuildingAmbienceComponent>(Owner, NAME_None, RF_Transactional);
    Owner->AddInstanceComponent(First);
    Owner->AddInstanceComponent(Second);
    First->RegisterComponent();
    Second->RegisterComponent();
    const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Targets{First, Second};
    {
        const FScopedTransaction Previous(FText::FromString(TEXT("Prior user edit")));
        Owner->Modify();
        Owner->Tags.Add(TEXT("PreviousUserEdit"));
    }
    FString Error;
    bool bInjected = false;
    TestFalse(TEXT("Mid-batch cancellation fails"),
              FAudioComponentDeletionOperation::Execute(Targets, World, Error,
                                                        [First, Owner, &bInjected]()
                                                        {
                                                            if (!IsValid(First))
                                                            {
                                                                Owner->Tags.Add(
                                                                    TEXT("ExternalOwnerEdit"));
                                                                bInjected = true;
                                                                return false;
                                                            }
                                                            return true;
                                                        }));
    TestTrue(TEXT("Failure actually injected"), bInjected);
    TestTrue(TEXT("Same first object restored"), IsValid(First));
    TestTrue(TEXT("Instance membership restored"), Owner->GetInstanceComponents().Contains(First));
    TestTrue(TEXT("Registration restored"), First->IsRegistered());
    TestTrue(TEXT("Other component preserved"), IsValid(Second));
    TestTrue(TEXT("External owner edit preserved"),
             Owner->Tags.Contains(TEXT("ExternalOwnerEdit")));
    TestTrue(TEXT("Failure explained"), !Error.IsEmpty());
    GEditor->UndoTransaction();
    TestFalse(TEXT("Previous Undo still reachable"),
              Owner->Tags.Contains(TEXT("PreviousUserEdit")));
    {
        const FScopedTransaction Caller(FText::FromString(TEXT("Caller-owned transaction")));
        TestFalse(TEXT("Nested entry refused"),
                  FAudioComponentDeletionOperation::Execute(Targets, World, Error));
        TestTrue(TEXT("Caller transaction retained"), GEditor->IsTransactionActive());
        TestTrue(TEXT("Rejected call preserves record"), IsValid(First));
    }
    TestTrue(TEXT("Successful batch"),
             FAudioComponentDeletionOperation::Execute(Targets, World, Error));
    TestFalse(TEXT("First destroyed"), IsValid(First));
    TestFalse(TEXT("Second destroyed"), IsValid(Second));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Successful batch Undo restores first"), IsValid(First));
    TestTrue(TEXT("Successful batch Undo restores second"), IsValid(Second));
    return true;
}
#endif
