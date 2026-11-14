#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HeadphoneCapture.h"
#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Systems/Audio/WandererHeadphoneSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include <atomic>

namespace HeadphoneTest
{
    // Static POD only: the audio-thread callback never dereferences test/UObject state.
    struct FMeasurement
    {
        std::atomic<uint64> Count{0}, Total{0}, Plugin{0}, Peak{0}, Voices{0};
    };
    FMeasurement Measurements[3];
    std::atomic<int32> Phase{0};

    void Monitor(const AkResourceMonitorDataSummary* Data)
    {
        if (!Data) return;
        auto& Sample = Measurements[Phase.load()];
        const uint64 CPU = uint64(FMath::Max(0.f, Data->totalCPU) * 1000);
        Sample.Count.fetch_add(1);
        Sample.Total.fetch_add(CPU);
        Sample.Plugin.fetch_add(uint64(FMath::Max(0.f, Data->pluginCPU) * 1000));
        // One writer: Wwise calls this at the end of the audio render frame.
        Sample.Peak.store(FMath::Max(Sample.Peak.load(), CPU));
        Sample.Voices.store(FMath::Max(Sample.Voices.load(), uint64(Data->totalVoices)));
    }

    void Pump(IWwiseSoundEngineAPI& Engine, double Seconds)
    {
        const double Until = FPlatformTime::Seconds() + Seconds;
        do { Engine.RenderAudio(); FPlatformProcess::Sleep(.01f); }
        while (FPlatformTime::Seconds() < Until);
    }

    struct FSession
    {
        IWwiseSoundEngineAPI& Engine;
        TArray<AkGameObjectID> Objects;
        bool bMonitor = false;
        bool bCapture = false;
        explicit FSession(IWwiseSoundEngineAPI& InEngine) : Engine(InEngine) {}
        ~FSession()
        {
            if (bCapture) Engine.StopOutputCapture();
            if (bMonitor) Engine.UnregisterResourceMonitorCallback(Monitor);
            Engine.SetRTPCValue("UsingHeadphone", 0.f);
            for (auto ID : Objects) { Engine.StopAll(ID); Engine.UnregisterGameObj(ID); }
            Engine.RenderAudio();
        }
        bool Register(AkGameObjectID ID)
        {
            if (Engine.RegisterGameObj(ID, "HeadphoneQA") != AK_Success) return false;
            Objects.Add(ID);
            return true;
        }
    };

    bool Capture(FSession& Session, const FString& Path)
    {
        Session.bCapture = Session.Engine.StartOutputCapture(*Path) == AK_Success;
        if (!Session.bCapture) return false;
        Pump(Session.Engine, 2.5);
        const bool bStopped = Session.Engine.StopOutputCapture() == AK_Success;
        Session.bCapture = !bStopped;
        return bStopped;
    }

    void CaptureSides(FSession& Session, UWandererHeadphoneSubsystem& Settings, AkGameObjectID Source, FAutomationTestBase& Test)
    {
        auto* Engine = &Session.Engine;
        AkSoundPosition Pose;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            Pose.Set(Side ? 300 : -300, 0, 0, 0, 0, 1, 0, 1, 0);
            Engine->SetPosition(Source, Pose);
            for (int32 Enabled = 0; Enabled < 2; ++Enabled)
            {
                Settings.SetUsingHeadphone(Enabled != 0);
                Pump(*Engine, .25);
                Phase=Enabled;
                // Unreal's Wwise IO hook resolves capture names under Saved/Wwise.
                Test.TestTrue(TEXT("Captured side and bypass state"), Capture(Session, FString::Printf(TEXT("headphone_side%d_on%d.wav"), Side, Enabled)));
            }
        }
    }

    double ToggleRepeatedly(FSession& Session, UWandererHeadphoneSubsystem& Settings, FAutomationTestBase& Test)
    {
        auto* Engine = &Session.Engine;
        Phase=2;
        double MaxCallMS = 0;
        Session.bCapture = Engine->StartOutputCapture(TEXT("headphone_toggle.wav")) == AK_Success;
        Test.TestTrue(TEXT("Toggle capture started"), Session.bCapture);
        for (int32 Index = 0; Index < 100; ++Index)
        {
            const bool bEnabled = (Index % 2) != 0;
            const double Begin = FPlatformTime::Seconds();
            Settings.SetUsingHeadphone(bEnabled);
            MaxCallMS = FMath::Max(MaxCallMS, (FPlatformTime::Seconds()-Begin)*1000);
            Pump(*Engine, .05);
            AkRtpcValue Value=0;
            auto Scope = AK::SoundEngine::Query::RTPCValue_Global;
            Test.TestEqual(TEXT("Query global switch"), Engine->Query->GetRTPCValue("UsingHeadphone", AK_INVALID_GAME_OBJECT, AK_INVALID_PLAYING_ID, Value, Scope), AK_Success);
            Test.TestEqual(TEXT("UI state matches engine"), Value, bEnabled ? 1.f : 0.f);
            Test.TestEqual(TEXT("Subsystem flag matches"), Settings.IsUsingHeadphone(), bEnabled);
        }
        if (Session.bCapture) { Engine->StopOutputCapture(); Session.bCapture=false; }
        return MaxCallMS;
    }

    FString Summary(double MaxCallMS)
    {
        FString Text = FString::Printf(TEXT("{\"max_switch_call_ms\":%.6f,\"phases\":["), MaxCallMS);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            auto& M = Measurements[Index];
            const double Count = FMath::Max(uint64(1), M.Count.load());
            Text += FString::Printf(TEXT("%s{\"phase\":%d,\"samples\":%llu,\"mean_cpu_percent\":%.6f,\"mean_plugin_percent\":%.6f,\"peak_cpu_percent\":%.6f,\"max_voices\":%llu}"),
                Index ? TEXT(",") : TEXT(""), Index, M.Count.load(), M.Total.load()/Count/1000,
                M.Plugin.load()/Count/1000, M.Peak.load()/1000., M.Voices.load());
        }
        return Text + TEXT("]}");
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadphoneTest, "Wanderer.Audio.Headphone.SwitchAndPan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeadphoneTest::RunTest(const FString& Parameters)
{
    using namespace HeadphoneTest;
    auto* Engine = IWwiseSoundEngineAPI::Get();
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeadphoneIsolatedProbe")) || !Engine ||
        !FAkAudioDevice::IsInitialized() || (GEditor && GEditor->PlayWorld))
    { AddError(TEXT("Requires isolated non-PIE -HeadphoneIsolatedProbe process with native audio.")); return false; }
    FSession Session(*Engine);
    for (auto& M : Measurements) { M.Count=0; M.Total=0; M.Plugin=0; M.Peak=0; M.Voices=0; }
    Phase=0;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UWandererHeadphoneSubsystem> Settings(NewObject<UWandererHeadphoneSubsystem>(Instance.Get()));
    TStrongObjectPtr<UAkAudioEvent> Play(LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Objects/Play_Radio.Play_Radio")));
    if (!TestTrue(TEXT("Event exists"), Play.IsValid())) return false;
    Play->LoadData();
    for (int32 Attempt = 0; Attempt < 500 && !Play->IsDataFullyLoaded(); ++Attempt) Pump(*Engine, .01);
    if (!TestTrue(TEXT("Media loaded"), Play->IsDataFullyLoaded())) return false;
    constexpr AkGameObjectID Listener = 0x48454144514100;
    constexpr AkGameObjectID Source = Listener + 1;
    if (!Session.Register(Listener) || !Session.Register(Source)) return false;
    AkSoundPosition Pose;
    Pose.Set(0, 0, 0, 0, 0, 1, 0, 1, 0);
    Engine->SetPosition(Listener, Pose);
    Engine->SetListeners(Source, &Listener, 1);
    Engine->SetRTPCValue("DeviceGain", 1.f, Source);
    Engine->SetSwitch("RadioPlayback", FParse::Param(FCommandLine::Get(), TEXT("HeadphoneSongProbe")) ? "Song" : "Hiss", Source);
    const AkPlayingID ID = Engine->PostEvent(Play->GetShortID(), Source);
    if (!TestTrue(TEXT("Radio posted"), ID != AK_INVALID_PLAYING_ID)) return false;
    Session.bMonitor = Engine->RegisterResourceMonitorCallback(Monitor) == AK_Success;
    TestTrue(TEXT("Native resource monitor registered"), Session.bMonitor);
    const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../AgentKit/runtime/headphone-20261111/audio"));
    IFileManager::Get().MakeDirectory(*Folder, true);
    HeadphoneCapture::FScopedCapture NativeCapture(*Engine);
    CaptureSides(Session, *Settings, Source, *this);
    const double MaxCallMS = ToggleRepeatedly(Session, *Settings, *this);
    // Direct SDK posts are not registered in FAkAudioDevice's UE-side event bookkeeping.
    AkSourcePosition SourcePosition{};
    AkUInt32 SourceCount = 1;
    TestEqual(TEXT("Original SDK PlayingID still has a source"), Engine->GetSourcePlayPositions(ID, &SourcePosition, &SourceCount, false), AK_Success);
    TestEqual(TEXT("Exactly one source after toggles"), SourceCount, AkUInt32(1));
    TestTrue(TEXT("Source clock advanced"), SourcePosition.msTime > 0);
    TestTrue(TEXT("Saved direct native output"), NativeCapture.Save(Folder/TEXT("native.f32")));
    const FString Result = Summary(MaxCallMS);
    TestTrue(TEXT("Saved CPU measurements"), FFileHelper::SaveStringToFile(Result, *(Folder/TEXT("performance.json"))));
    AddInfo(Result);
    Settings->Deinitialize();
    return true;
}
#endif
