#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"

#include "Systems/Audio/ListenerAudioManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FListenerAudioStopTest,
    "Wanderer.Audio.Listener.TerminalStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FListenerAudioStopTest::RunTest(const FString& InParameters)
{
    const TStrongObjectPtr<UListenerAudioManager> Session(NewObject<UListenerAudioManager>());
    Session->bInDeviceCall = true;
    Session->bEarsUnderwater = true;
    Session->Stop(nullptr);
    TestTrue(TEXT("Stop is terminal immediately"), Session->bStopped);
    TestTrue(TEXT("In-flight cleanup is deferred"), Session->bStopPending);
    Session->FinishDeviceCall();
    TestFalse(TEXT("Cleanup completed after call"), Session->bStopPending);
    TestFalse(TEXT("Local water state released"), Session->AreEarsUnderwater());
    FListenerWaterSample Sample;
    Sample.bHasSurface = true;
    Sample.DepthCm = 100.0f;
    Sample.SampleTimeSec = 100.0;
    Session->UpdateWater(Sample);
    TestFalse(TEXT("Stopped session rejects water input"), Session->bPendingUnderwater);
    Session->Stop(nullptr);
    TestFalse(TEXT("Repeated Stop is safe"), Session->AreEarsUnderwater());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FListenerMixRecoveryTest,
    "Wanderer.Audio.Listener.MenuRecoveryAndWaterState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FListenerMixRecoveryTest::RunTest(const FString& InParameters)
{
    const TStrongObjectPtr<UListenerAudioManager> Session(NewObject<UListenerAudioManager>());
    const TStrongObjectPtr<UAkAudioEvent> Pause(LoadObject<UAkAudioEvent>(
        nullptr, TEXT("/Game/WwiseAudio/Events/UI/Pause_Menu_SFX")));
    const TStrongObjectPtr<UAkAudioEvent> Resume(LoadObject<UAkAudioEvent>(
        nullptr, TEXT("/Game/WwiseAudio/Events/UI/Resume_Menu_SFX")));
    ON_SCOPE_EXIT
    {
        Session->Stop(Resume.Get());
    };
    if (!TestTrue(TEXT("Wwise and bundled menu events are available"),
            FAkAudioDevice::IsInitialized() && Pause.IsValid() && Resume.IsValid()))
    {
        return false;
    }
    Session->SetMenuState(EListenerMenuMode::Paused, Pause.Get(), Resume.Get());
    if (!TestTrue(TEXT("Pause action was submitted"), Session->bMenuAudioPaused))
    {
        return false;
    }
    Session->SetMenuState(EListenerMenuMode::Closed, nullptr, nullptr);
    TestTrue(TEXT("Resume failure leaves an explicit retry"), Session->bMenuResumePending);
    TestTrue(TEXT("Pause is outstanding until Resume is accepted"), Session->bMenuAudioPaused);

    FListenerWaterSample Sample;
    Sample.bHasSurface = true;
    Sample.DepthCm = 7.0f;
    Sample.SampleTimeSec = 1.0;
    Session->UpdateWater(Sample);
    TestFalse(TEXT("A single submerged sample does not switch the mix"),
        Session->AreEarsUnderwater());
    Sample.SampleTimeSec = 1.3;
    Session->UpdateWater(Sample);
    TestTrue(TEXT("Water state changes while menu recovery is pending"),
        Session->AreEarsUnderwater());

    Session->ResumeEvent = Resume.Get();
    Session->RetryPendingMix();
    TestFalse(TEXT("Retry clears the pending menu state"), Session->bMenuResumePending);
    TestFalse(TEXT("Retry balances the pause"), Session->bMenuAudioPaused);
    TestTrue(TEXT("Menu recovery does not reset underwater state"), Session->AreEarsUnderwater());
    Session->SetMenuState(EListenerMenuMode::Paused, Pause.Get(), Resume.Get());
    TestTrue(TEXT("A later pause can be submitted"), Session->bMenuAudioPaused);
    Session->SetMenuState(EListenerMenuMode::Closed, nullptr, Resume.Get());
    Session->ResetWater(true);
    TestFalse(TEXT("Unpossess or teardown can reset water state"), Session->AreEarsUnderwater());
    return true;
}
#endif
