#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/DataTable.h"
#include "Engine/World.h"

#include "Tests/AudioRuntime/AudioSchedulingTestSource.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Systems/WandererEnvironmentSubsystem.h"

struct FAudioSchedulingTestAccess
{
    static void Suspend(UWandererAudioSubsystem& InSystem)
    {
        InSystem.SuspendSources();
    }

    static void Schedule(UWandererAudioSubsystem& InSystem)
    {
        InSystem.ResolveBudget();
        InSystem.Schedule(FVector::ZeroVector);
    }

    static void UpdateResources(UWandererAudioSubsystem& InSystem, const FVector& InListener)
    {
        InSystem.ResolveBudget();
        InSystem.UpdateResourceGroups(InListener);
    }
};

namespace
{
struct FAudioSchedulingTestWorld
{
    UWorld* World = nullptr;

    FAudioSchedulingTestWorld()
    {
        const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                        .AllowAudioPlayback(false)
                                                        .CreatePhysicsScene(false)
                                                        .CreateNavigation(false)
                                                        .CreateAISystem(false)
                                                        .ShouldSimulatePhysics(false)
                                                        .SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                                    ERHIFeatureLevel::Num, &Values);
    }

    ~FAudioSchedulingTestWorld()
    {
        if (World)
        {
            World->DestroyWorld(false);
        }
    }

    UAudioSchedulingTestSource* CreateSource(FName InName)
    {
        constexpr int32 TestProfileLimit = 8;
        UAudioSchedulingTestSource* Source = NewObject<UAudioSchedulingTestSource>(World, InName);
        Source->Info.ProfileKey = InName.ToString();
        Source->Info.OwnerPath = InName.ToString();
        Source->Info.MaxInstances = TestProfileLimit;
        return Source;
    }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSchedulingRegistrationTest,
                                 "Wanderer.Audio.Scheduling.Registration",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FAudioSchedulingRegistrationTest::RunTest(const FString& Parameters)
{
    FAudioSchedulingTestWorld First;
    FAudioSchedulingTestWorld Second;
    if (!TestNotNull(TEXT("First world"), First.World)
        || !TestNotNull(TEXT("Second world"), Second.World))
    {
        return false;
    }

    UWandererAudioSubsystem* System = First.World->GetSubsystem<UWandererAudioSubsystem>();
    UAudioSchedulingTestSource* Source = First.CreateSource(TEXT("Source"));
    UAudioSchedulingTestSource* Other = First.CreateSource(TEXT("Other"));
    Source->RoomPaths.Add(FSoftObjectPath(TEXT("/Game/TestRoom.TestRoom")));
    Other->RoomPaths = Source->RoomPaths;
    System->RegisterSource(Source);
    System->RegisterSource(Source);
    System->RegisterSource(Second.CreateSource(TEXT("Foreign")));
    System->RegisterSource(First.World);
    TestEqual(TEXT("Unique, same-world interface registrations only"), System->GetRegisteredCount(),
              1);

    AddExpectedError(TEXT("Building ambience Room membership conflict"),
                     EAutomationExpectedErrorFlags::Contains, 1);
    System->RegisterSource(Other);
    TestEqual(TEXT("Conflicting member rejected atomically"), System->GetRegisteredCount(), 1);
    System->UnregisterSource(Source);
    System->RegisterSource(Other);
    TestEqual(TEXT("Unregister releases room membership"), System->GetRegisteredCount(), 1);
    System->Deinitialize();
    TestEqual(TEXT("Remaining source stopped once"), Other->ShutdownCount, 1);
    TestEqual(TEXT("Unregistered source not stopped by subsystem"), Source->ShutdownCount, 0);
    System->RegisterSource(Source);
    TestEqual(TEXT("Teardown refuses new registrations"), System->GetRegisteredCount(), 0);
    System->Deinitialize();
    TestEqual(TEXT("Repeated teardown does not stop twice"), Other->ShutdownCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSchedulingAdmissionTest,
                                 "Wanderer.Audio.Scheduling.Admission",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FAudioSchedulingAdmissionTest::RunTest(const FString& Parameters)
{
    FAudioSchedulingTestWorld Fixture;
    if (!TestNotNull(TEXT("World"), Fixture.World))
    {
        return false;
    }

    UWandererAudioSubsystem* System = Fixture.World->GetSubsystem<UWandererAudioSubsystem>();
    AWandererWorldAudioBudget* Budget = Fixture.World->SpawnActor<AWandererWorldAudioBudget>();
    Budget->MaxTotalEvents = 1;
    UAudioSchedulingTestSource* First = Fixture.CreateSource(TEXT("First"));
    UAudioSchedulingTestSource* Second = Fixture.CreateSource(TEXT("Second"));
    First->Info.Priority = 11.0f;
    First->Info.bPlaying = true;
    Second->Info.Priority = 12.0f;
    System->RegisterSource(Second);
    System->RegisterSource(First);
    FAudioSchedulingTestAccess::Schedule(*System);
    TestTrue(TEXT("Existing playback retains its priority bias"), First->bAdmitted);
    TestFalse(TEXT("Total cap rejects next source"), Second->bAdmitted);

    First->Info.bProtected = true;
    Second->Info.bProtected = true;
    FAudioSchedulingTestAccess::Schedule(*System);
    TestEqual(TEXT("Protected sources may overflow"), System->GetAdmittedCount(), 2);
    TestEqual(TEXT("Overflow is reported"), System->GetProtectedOverflow(), 1);
    Second->PlaybackIdentity = First;
    First->PlaybackIdentity = First;
    FAudioSchedulingTestAccess::Schedule(*System);
    TestFalse(TEXT("Protection never overrides duplicate playback identity"), Second->bAdmitted);
    TestEqual(TEXT("Duplicate reason retained"), Second->LastReason,
              FName(TEXT("Duplicate Room owner")));

    First->Info.bProtected = false;
    Second->Info.bProtected = false;
    First->Info.bPlaying = false;
    First->Info.Priority = Second->Info.Priority;
    First->PlaybackIdentity = nullptr;
    Second->PlaybackIdentity = nullptr;
    Budget->MaxTotalEvents = 2;
    First->Info.MaxInstances = 1;
    Second->Info.MaxInstances = 1;
    Second->Info.ProfileKey = First->Info.ProfileKey;
    Fixture.World->GetSubsystem<UWandererEnvironmentSubsystem>()->SetNight(true);
    FAudioSchedulingTestAccess::Schedule(*System);
    TestTrue(TEXT("Equal priorities use stable owner-path tie break"), First->bAdmitted);
    TestFalse(TEXT("Shared profile instance cap applies"), Second->bAdmitted);
    TestTrue(TEXT("Night arrives through environment contract"), First->bNight && Second->bNight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSchedulingResidencyTest,
                                 "Wanderer.Audio.Scheduling.Residency",
                                 EAutomationTestFlags::EditorContext
                                     | EAutomationTestFlags::EngineFilter)

bool FAudioSchedulingResidencyTest::RunTest(const FString& Parameters)
{
    FAudioSchedulingTestWorld Fixture;
    if (!TestNotNull(TEXT("World"), Fixture.World))
    {
        return false;
    }

    UWandererAudioSubsystem* System = Fixture.World->GetSubsystem<UWandererAudioSubsystem>();
    AWandererWorldAudioBudget* Budget = Fixture.World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup Group;
    Group.Name = TEXT("Region");
    Group.BoundsExtent = FVector::ZeroVector;
    Group.LoadDistanceM = 1.0f;
    Group.UnloadDistanceM = 2.0f;
    Group.UnloadDelaySec = 0.0f;
    Group.MaxEvents = 1;
    Budget->ResourceGroups.Add(Group);
    UAudioSchedulingTestSource* Source = Fixture.CreateSource(TEXT("DrainingSource"));
    Source->Info.ResourceGroup = Group.Name;
    Source->PlayingID = 1;
    System->RegisterSource(Source);
    TestTrue(TEXT("None is allowed before a budget snapshot"), System->IsResourceGroupActive(NAME_None));
    TestEqual(TEXT("Before initialization there is no unknown-group diagnosis"),
              System->GetResourceGroupStatus(Group.Name), FString(TEXT("Resource groups not initialized")));
    FAudioSchedulingTestAccess::UpdateResources(*System, FVector::ZeroVector);
    TestTrue(TEXT("Empty resource group becomes active"),
             System->IsResourceGroupActive(Group.Name));
    const FName Missing = TEXT("Regoin");
    TestFalse(TEXT("Unknown group cannot play"), System->IsResourceGroupActive(Missing));
    TestTrue(TEXT("Unknown group status names the misconfiguration"),
             System->GetResourceGroupStatus(Missing).Contains(Missing.ToString()));
    AWandererWorldAudioEmitter* Emitter = Fixture.World->SpawnActor<AWandererWorldAudioEmitter>();
    UWandererAudioSourceComponent* Audio =
        Emitter->FindComponentByClass<UWandererAudioSourceComponent>();
    Audio->ResourceGroup = Missing;
    TestFalse(TEXT("Source with a misspelled group is not admitted"),
              Audio->Evaluate(FVector::ZeroVector));
    TestEqual(TEXT("Source reports the same group error as the subsystem"),
              Audio->GetStatus(), System->GetResourceGroupStatus(Missing));
    Budget->ResourceGroups[0].MaxEvents = 9;
    TestEqual(TEXT("Resource policy is a session snapshot"),
              System->GetResourceGroupLimit(Group.Name), 1);

    constexpr double FarDistanceCentimeters = 1000.0;
    const FVector Far(FarDistanceCentimeters, 0.0, 0.0);
    FAudioSchedulingTestAccess::UpdateResources(*System, Far);
    TestEqual(TEXT("Posted ID keeps resources draining"),
              System->GetResourceGroupStatus(Group.Name), FString(TEXT("Draining")));
    Source->PlayingID = 0;
    FAudioSchedulingTestAccess::UpdateResources(*System, Far);
    TestEqual(TEXT("Resources release after posted ID clears"),
              System->GetResourceGroupStatus(Group.Name), FString(TEXT("Dormant")));
    FAudioSchedulingTestAccess::UpdateResources(*System, FVector::ZeroVector);
    TestTrue(TEXT("Region can be re-entered"), System->IsResourceGroupActive(Group.Name));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioResourceConfigurationFailureTest,
    "Wanderer.Audio.Scheduling.ResourceConfigurationFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioResourceConfigurationFailureTest::RunTest(const FString& InParameters)
{
    FAudioSchedulingTestWorld Fixture;
    UWandererAudioSubsystem* System = Fixture.World->GetSubsystem<UWandererAudioSubsystem>();
    AWandererWorldAudioBudget* Budget = Fixture.World->SpawnActor<AWandererWorldAudioBudget>();
    FWandererAudioResourceGroup First;
    First.Name = TEXT("First");
    First.Dependencies.Add(TEXT("Second"));
    FWandererAudioResourceGroup Second;
    Second.Name = TEXT("Second");
    Second.Dependencies.Add(First.Name);
    Budget->ResourceGroups = {First, Second};
    AddExpectedError(TEXT("Resource dependencies contain a cycle"),
                     EAutomationExpectedErrorFlags::Contains, 1);
    FAudioSchedulingTestAccess::UpdateResources(*System, FVector::ZeroVector);
    TestFalse(TEXT("Cycle blocks group activation"), System->IsResourceGroupActive(First.Name));
    TestEqual(TEXT("Failed snapshot reports the cycle"),
              System->GetResourceGroupStatus(First.Name), FString(TEXT("Resource dependencies contain a cycle")));
    Budget->ResourceGroups[1].Dependencies.Reset();
    FAudioSchedulingTestAccess::UpdateResources(*System, FVector::ZeroVector);
    TestFalse(TEXT("Live edits do not replace a rejected snapshot"),
              System->IsResourceGroupActive(First.Name));
    TestTrue(TEXT("None remains independent of invalid groups"),
             System->IsResourceGroupActive(NAME_None));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSchedulingReentryTest,
    "Wanderer.Audio.Scheduling.TeardownDuringEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSchedulingReentryTest::RunTest(const FString& InParameters)
{
    FAudioSchedulingTestWorld Fixture;
    UWandererAudioSubsystem* System = Fixture.World->GetSubsystem<UWandererAudioSubsystem>();
    UAudioSchedulingTestSource* Source = Fixture.CreateSource(TEXT("StoppingSource"));
    UAudioSchedulingTestSource* Other = Fixture.CreateSource(TEXT("OtherSource"));
    Source->bStopDuringEvaluation = true;
    System->RegisterSource(Source);
    System->RegisterSource(Other);
    FAudioSchedulingTestAccess::Schedule(*System);
    TestEqual(TEXT("Stopping source shuts down once"), Source->ShutdownCount, 1);
    TestEqual(TEXT("Other source shuts down once"), Other->ShutdownCount, 1);
    TestEqual(TEXT("No playback after teardown callback"), Source->UpdateCount, 0);
    TestEqual(TEXT("No later candidate playback"), Other->UpdateCount, 0);
    TestFalse(TEXT("Teardown rejects resource acquisition"), System->AcquireEventResource(nullptr));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSuspensionReentryTest,
    "Wanderer.Audio.Scheduling.TeardownDuringSuspension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSuspensionReentryTest::RunTest(const FString& InParameters)
{
    FAudioSchedulingTestWorld Fixture;
    UWandererAudioSubsystem* System = Fixture.World->GetSubsystem<UWandererAudioSubsystem>();
    UAudioSchedulingTestSource* Source = Fixture.CreateSource(TEXT("StoppingSource"));
    UAudioSchedulingTestSource* Other = Fixture.CreateSource(TEXT("OtherSource"));
    Source->bStopDuringUpdate = true;
    System->RegisterSource(Source);
    System->RegisterSource(Other);
    FAudioSchedulingTestAccess::Suspend(*System);
    TestEqual(TEXT("First source suspended"), Source->UpdateCount, 1);
    TestEqual(TEXT("Teardown stops both sources"), Other->ShutdownCount, 1);
    TestEqual(TEXT("No suspension callback after teardown"), Other->UpdateCount, 0);
    return true;
}
#endif
