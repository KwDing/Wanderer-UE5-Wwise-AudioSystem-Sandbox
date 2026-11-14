#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Systems/Audio/WandererHeadphoneSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeadphoneSettingsOwnershipTest,
    "Wanderer.Audio.Headphone.SettingsOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeadphoneSettingsOwnershipTest::RunTest(const FString& InParameters)
{
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeadphoneIsolatedProbe"))
        || !Engine || !FAkAudioDevice::IsInitialized())
    {
        AddError(TEXT("Requires an isolated -HeadphoneIsolatedProbe process with Wwise ready."));
        return false;
    }
    TStrongObjectPtr<UGameInstance> FirstInstance(NewObject<UGameInstance>());
    TStrongObjectPtr<UGameInstance> SecondInstance(NewObject<UGameInstance>());
    TStrongObjectPtr<UWandererHeadphoneSubsystem> First(
        NewObject<UWandererHeadphoneSubsystem>(FirstInstance.Get()));
    TStrongObjectPtr<UWandererHeadphoneSubsystem> Second(
        NewObject<UWandererHeadphoneSubsystem>(SecondInstance.Get()));
    TestTrue(TEXT("First setting accepted"), First->TrySetUsingHeadphone(true));
    TestTrue(TEXT("Second instance becomes latest writer"), Second->TrySetUsingHeadphone(true));
    Engine->RenderAudio();
    FPlatformProcess::Sleep(0.05f);
    First->Deinitialize();
    First->Deinitialize();
    TestTrue(TEXT("Non-owner teardown leaves output enabled"), Second->IsUsingHeadphone());
    TestFalse(TEXT("Stopped instance cannot write"), First->TrySetUsingHeadphone(false));
    TestTrue(TEXT("Stopped writer cannot alter live state"), Second->IsUsingHeadphone());
    Second->Deinitialize();
    Engine->RenderAudio();
    FPlatformProcess::Sleep(0.05f);
    AkRtpcValue Value = -1.0f;
    auto Scope = AK::SoundEngine::Query::RTPCValue_Global;
    TestEqual(TEXT("Output query after owner teardown"),
        Engine->Query->GetRTPCValue("UsingHeadphone", AK_INVALID_GAME_OBJECT,
            AK_INVALID_PLAYING_ID, Value, Scope), AK_Success);
    TestEqual(TEXT("Owner teardown restores speaker mix"), Value, 0.0f);
    TestFalse(TEXT("Owner terminal state rejects writes"), Second->TrySetUsingHeadphone(true));
    return true;
}
#endif
