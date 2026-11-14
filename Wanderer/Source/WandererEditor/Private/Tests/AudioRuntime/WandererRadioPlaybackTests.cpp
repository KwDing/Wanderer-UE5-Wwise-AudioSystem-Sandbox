#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "Editor.h"
#include "HAL/PlatformProcess.h"
#include "UObject/StrongObjectPtr.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

namespace WandererRadioTests
{
    // Native IDs are limited to this non-PIE test and released on every exit.
    struct FScopedGameObjects
    {
        IWwiseSoundEngineAPI& Engine;
        TArray<AkGameObjectID> Registered;

        explicit FScopedGameObjects(IWwiseSoundEngineAPI& InEngine) : Engine(InEngine) {}
        ~FScopedGameObjects()
        {
            for (AkGameObjectID ID : Registered)
            {
                Engine.StopAll(ID);
                Engine.UnregisterGameObj(ID);
            }
            Engine.RenderAudio();
        }

        bool Register(AkGameObjectID ID)
        {
            if (Engine.RegisterGameObj(ID, "WandererRadioQA") != AK_Success) return false;
            Registered.Add(ID);
            return true;
        }
    };

    void RenderCommands(IWwiseSoundEngineAPI& Engine)
    {
        // The SDK audio thread runs while the test's game-thread call is waiting.
        for (int32 Index = 0; Index < 10; ++Index)
        {
            Engine.RenderAudio();
            FPlatformProcess::Sleep(0.02f);
        }
    }

    bool GetSource(IWwiseSoundEngineAPI& Engine, AkPlayingID ID, AkSourcePosition& OutSource)
    {
        AkUInt32 Count = 1;
        return Engine.GetSourcePlayPositions(ID, &OutSource, &Count, false) == AK_Success && Count == 1;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWandererRadioIsolationTest, "Wanderer.RadioPlayback.NativeSwitchAndScopedStop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWandererRadioIsolationTest::RunTest(const FString& Parameters)
{
    using namespace WandererRadioTests;
    auto* Engine = IWwiseSoundEngineAPI::Get();
    if (!Engine || !FAkAudioDevice::IsInitialized() || (GEditor && GEditor->PlayWorld))
    {
        AddError(TEXT("Run outside PIE with initialized Wwise audio; do not use -nosound."));
        return false;
    }
    TStrongObjectPtr<UAkAudioEvent> Play(LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Objects/Play_Radio.Play_Radio")));
    TStrongObjectPtr<UAkAudioEvent> Stop(LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Objects/Stop_Radio.Stop_Radio")));
    if (!TestTrue(TEXT("Shared Radio Event assets exist"), Play.IsValid() && Stop.IsValid())) return false;
    TestEqual(TEXT("Direct SDK Switch uses Event-owned media dependencies"), Play->EventInfo.SwitchContainerLoading, EWwiseEventSwitchContainerLoading::AlwaysLoad);
    Play->LoadData();
    Stop->LoadData();

    FScopedGameObjects Objects(*Engine);
    constexpr AkGameObjectID Listener = 0x57414E4452514100;
    if (!TestTrue(TEXT("Register test listener"), Objects.Register(Listener))) return false;
    const AkGameObjectID IDs[] = {Listener + 1, Listener + 2, Listener + 3, Listener + 4};
    const ANSICHAR* Values[] = {nullptr, "Noise", "Hiss", "Song"};
    AkPlayingID PlayingIDs[4] = {};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (!TestTrue(TEXT("Register isolated radio game object"), Objects.Register(IDs[Index]))) return false;
        TestEqual(TEXT("Assign test listener"), Engine->SetListeners(IDs[Index], &Listener, 1), AK_Success);
        TestEqual(TEXT("Set audible device gain"), Engine->SetRTPCValue("DeviceGain", 1.0f, IDs[Index]), AK_Success);
        if (Values[Index]) TestEqual(TEXT("Set per-object content"), Engine->SetSwitch("RadioPlayback", Values[Index], IDs[Index]), AK_Success);
        PlayingIDs[Index] = Engine->PostEvent(Play->GetShortID(), IDs[Index]);
        if (!TestTrue(TEXT("Post shared Play_Radio"), PlayingIDs[Index] != AK_INVALID_PLAYING_ID)) return false;
    }
    RenderCommands(*Engine);
    AkSourcePosition Sources[4] = {};
    for (int32 Index = 0; Index < 4; ++Index)
        if (!TestTrue(TEXT("Exactly one selected source is playing"), GetSource(*Engine, PlayingIDs[Index], Sources[Index]))) return false;

    TestEqual(TEXT("Unset Switch falls back to Song"), Sources[0].audioNodeID, Sources[3].audioNodeID);
    TestTrue(TEXT("Noise differs from Song"), Sources[1].audioNodeID != Sources[3].audioNodeID);
    TestTrue(TEXT("Hiss differs from Noise and Song"), Sources[2].audioNodeID != Sources[1].audioNodeID && Sources[2].audioNodeID != Sources[3].audioNodeID);
    TestTrue(TEXT("Source playback progressed"), Sources[3].msTime > 0);

    const AkPlayingID StopID = Engine->PostEvent(Stop->GetShortID(), IDs[0]);
    TestTrue(TEXT("Post container Stop on only the first object"), StopID != AK_INVALID_PLAYING_ID);
    RenderCommands(*Engine);
    AkSourcePosition After = {};
    TestFalse(TEXT("Stopped object no longer has a source"), GetSource(*Engine, PlayingIDs[0], After));
    for (int32 Index = 1; Index < 4; ++Index)
    {
        TestTrue(TEXT("Other content groups remain active"), GetSource(*Engine, PlayingIDs[Index], After));
        TestEqual(TEXT("Other groups keep their selected content"), After.audioNodeID, Sources[Index].audioNodeID);
        TestTrue(TEXT("Other groups continue advancing"), After.msTime > Sources[Index].msTime);
    }
    return true;
}
#endif
