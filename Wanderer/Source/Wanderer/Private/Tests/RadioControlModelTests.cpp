#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Actors/RadioInventoryActor.h"
#include "Actors/WandererSpeakerActor.h"
#include "Framework/RadioControlModel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioControlModelTest,
    "Wanderer.RadioPlayback.ModelProviderLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadioControlModelTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Isolated world"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    TStrongObjectPtr<URadioControlModel> Model(NewObject<URadioControlModel>());
    ARadioInventoryActor* First = World->SpawnActor<ARadioInventoryActor>();
    ARadioInventoryActor* Second = World->SpawnActor<ARadioInventoryActor>();
    if (!TestNotNull(TEXT("First provider"), First)
        || !TestNotNull(TEXT("Replacement provider"), Second))
    {
        return false;
    }
    TestTrue(TEXT("Register provider"), Model->RegisterProvider(First));
    TestFalse(TEXT("Do not silently replace live provider"), Model->RegisterProvider(Second));
    Model->UnregisterProvider(First);
    TestTrue(TEXT("Register replacement after explicit teardown"), Model->RegisterProvider(Second));
    Model->UnregisterProvider(First);
    TestFalse(
        TEXT("Late unregister cannot erase current provider"), Model->RegisterProvider(First));
    Second->Destroy();
    TestFalse(TEXT("Dead entity has unavailable snapshot"), Model->GetSnapshot().bAvailable);
    TestTrue(TEXT("Dead entity has empty device list"), Model->GetSnapshot().Devices.IsEmpty());
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::TogglePlayback;
    TestFalse(TEXT("Command to destroyed provider rejected"), Model->Execute(Request));
    TestNull(
        TEXT("Destroyed preview has no texture"), Model->GetPreview(EWandererSpeakerItem::Radio));
    TestTrue(TEXT("Stable model accepts new provider"), Model->RegisterProvider(First));
    AWandererSpeakerActor* DeviceA = World->SpawnActor<AWandererSpeakerActor>();
    AWandererSpeakerActor* DeviceB = World->SpawnActor<AWandererSpeakerActor>();
    if (DeviceA && DeviceB)
    {
        DeviceA->SetDeviceNumber(1);
        DeviceB->SetDeviceNumber(1);
        TestTrue(TEXT("Reused display slot cannot alias device identity"),
            DeviceA->GetDeviceID() != DeviceB->GetDeviceID());
    }
    if (DeviceA)
    {
        DeviceA->RequestPlayback(TEXT("Song"), false);
        DeviceA->Destroy();
        DeviceA->RequestPlayback(TEXT("Song"), true);
        TestFalse(TEXT("Destroyed device rejects direct playback restart"),
            DeviceA->IsPlaybackRequested());
    }
    return true;
}
#endif
