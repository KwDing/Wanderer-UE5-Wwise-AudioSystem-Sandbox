#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"

#include "AkAudioEvent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Features/AudioManager/Authoring/BuildingAudioEditOperation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingAudioEditRecoveryTest,
                                 "Wanderer.Audio.Authoring.BuildingEditRecovery",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FBuildingAudioEditRecoveryTest::RunTest(const FString& InParameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    AWandererWorldAudioBudget* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    AAkSpatialAudioVolume* Room = World->SpawnActor<AAkSpatialAudioVolume>();
    UAkAudioEvent* Event = NewObject<UAkAudioEvent>();
    FString Error;
    UWandererBuildingAmbienceComponent* Cancelled = FBuildingAudioEditOperation::Create(
        {Room}, TEXT("Cancelled"), Event, Error, [Budget]()
        { return Budget->FindComponentByClass<UWandererBuildingAmbienceComponent>() == nullptr; });
    TestNull(TEXT("Creation cancelled after component registration"), Cancelled);
    TestNull(TEXT("Created component cleaned"),
             Budget->FindComponentByClass<UWandererBuildingAmbienceComponent>());
    UWandererBuildingAmbienceComponent* Building
        = FBuildingAudioEditOperation::Create({Room}, TEXT("House"), Event, Error);
    if (!TestNotNull(TEXT("Normal creation succeeds"), Building))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Room membership retained"), Building->ContainsRoom(Room));
    FWandererAudioResourceGroup Group;
    Group.Name = TEXT("City");
    Budget->ResourceGroups.Add(Group);
    TestFalse(TEXT("Changed registry invalidates commit"),
              FBuildingAudioEditOperation::ApplyResourceGroup(
                  {Building}, Group.Name, World, Error,
                  [Building, Budget]()
                  {
                      if (Building->ResourceGroup == TEXT("City"))
                      {
                          Budget->ResourceGroups.Reset();
                      }
                      return true;
                  }));
    TestTrue(TEXT("Written group compensated"), Building->ResourceGroup.IsNone());
    TestTrue(TEXT("External registry edit retained"), Budget->ResourceGroups.IsEmpty());
    Budget->ResourceGroups.Add(Group);
    TestFalse(TEXT("A final callback cannot overwrite the group after validation"),
        FBuildingAudioEditOperation::ApplyResourceGroup({Building}, Group.Name, World, Error,
            [Building]()
            {
                if (Building->ResourceGroup == TEXT("City"))
                {
                    Building->ResourceGroup = TEXT("External");
                }
                return true;
            }));
    TestEqual(TEXT("External group edit survives compensation"), Building->ResourceGroup,
              FName(TEXT("External")));
    Building->ResourceGroup = NAME_None;
    TestTrue(TEXT("Normal group assignment succeeds"),
             FBuildingAudioEditOperation::ApplyResourceGroup({Building}, Group.Name, World, Error));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Group assignment has its own Undo"), Building->ResourceGroup.IsNone());
    TestNull(TEXT("Duplicate room membership refused"),
             FBuildingAudioEditOperation::Create({Room}, TEXT("Duplicate"), Event, Error));
    return true;
}
#endif
