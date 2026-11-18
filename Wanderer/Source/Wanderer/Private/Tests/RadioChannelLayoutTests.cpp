#include "Systems/Audio/RadioChannelLayout.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "AK/SoundEngine/Common/AkSpeakerConfig.h"

#include "Systems/Audio/WwiseAudioBackend.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioChannelLayoutTest,
    "Wanderer.RadioPlayback.Layout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadioChannelLayoutTest::RunTest(const FString& InParameters)
{
    const FName Song(TEXT("Song"));
    TArray<FRadioEmitterSnapshot> Sources;
    for (int32 Number : {4, 2, 1, 3})
    {
        FRadioEmitterSnapshot& Source = Sources.AddDefaulted_GetRef();
        Source.Number = Number;
        Source.ContentID = Number == 2 ? FName(TEXT("Noise")) : Song;
        Source.bPlaying = Number == 3;
    }

    const FRadioChannelLayout Stereo(Sources, Song, true);
    TestEqual(TEXT("First paused member retains left"),
        Stereo.GetChannel(0), ERadioChannel::Left);
    TestEqual(TEXT("Other content excluded"), Stereo.GetChannel(1), ERadioChannel::None);
    TestEqual(TEXT("Third device gets right by number"),
        Stereo.GetChannel(2), ERadioChannel::Right);
    TestEqual(TEXT("Unpaired member gets both channels"),
        Stereo.GetChannel(3), ERadioChannel::Stereo);
    TestTrue(TEXT("One playing member keeps group clock active"), Stereo.HasPlaybackRequest());
    TestTrue(
        TEXT("Two stereo copies"), FMath::IsNearlyEqual(Stereo.GetGain(), FMath::InvSqrt(2.0f)));

    Sources.Pop();
    const FRadioChannelLayout Reduced(Sources, Song, true);
    TestNull(TEXT("Removed member excluded from next snapshot"), Reduced.GetMember(2));
    TestEqual(TEXT("Remaining pair closes rank gap"),
        Reduced.GetChannel(3), ERadioChannel::Right);
    TestFalse(TEXT("Remaining members paused"), Reduced.HasPlaybackRequest());
    TestEqual(TEXT("One stereo copy"), Reduced.GetGain(), 1.0f);

    const FRadioChannelLayout Mono(Sources, Song, false);
    TestEqual(TEXT("Mono uses center channel"), Mono.GetChannel(3), ERadioChannel::Center);
    TestTrue(TEXT("Equal power for two mono members"),
        FMath::IsNearlyEqual(Mono.GetGain(), FMath::InvSqrt(2.0f)));

    TestEqual(TEXT("Native left mask"), WandererAudioBackend::GetRadioChannelMask(ERadioChannel::Left),
        int32(AK_SPEAKER_FRONT_LEFT));
    TestEqual(TEXT("Native right mask"), WandererAudioBackend::GetRadioChannelMask(ERadioChannel::Right),
        int32(AK_SPEAKER_FRONT_RIGHT));
    TestEqual(TEXT("Native stereo mask"), WandererAudioBackend::GetRadioChannelMask(ERadioChannel::Stereo),
        int32(AK_SPEAKER_SETUP_STEREO));
    TestEqual(TEXT("Native center mask"), WandererAudioBackend::GetRadioChannelMask(ERadioChannel::Center),
        int32(AK_SPEAKER_FRONT_CENTER));
    TestEqual(TEXT("Empty channel mask"), WandererAudioBackend::GetRadioChannelMask(ERadioChannel::None), 0);
    return true;
}

#endif
