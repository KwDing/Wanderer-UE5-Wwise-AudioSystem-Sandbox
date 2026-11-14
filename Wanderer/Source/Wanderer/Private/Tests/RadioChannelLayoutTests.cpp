#include "Systems/Audio/RadioChannelLayout.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "AK/SoundEngine/Common/AkSpeakerConfig.h"

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
        Stereo.GetChannelMask(0),
        int32(AK_SPEAKER_FRONT_LEFT));
    TestEqual(TEXT("Other content excluded"), Stereo.GetChannelMask(1), 0);
    TestEqual(TEXT("Third device gets right by number"),
        Stereo.GetChannelMask(2),
        int32(AK_SPEAKER_FRONT_RIGHT));
    TestEqual(TEXT("Unpaired member gets both channels"),
        Stereo.GetChannelMask(3),
        int32(AK_SPEAKER_SETUP_STEREO));
    TestTrue(TEXT("One playing member keeps group clock active"), Stereo.HasPlaybackRequest());
    TestTrue(
        TEXT("Two stereo copies"), FMath::IsNearlyEqual(Stereo.GetGain(), FMath::InvSqrt(2.0f)));

    Sources.Pop();
    const FRadioChannelLayout Reduced(Sources, Song, true);
    TestNull(TEXT("Removed member excluded from next snapshot"), Reduced.GetMember(2));
    TestEqual(TEXT("Remaining pair closes rank gap"),
        Reduced.GetChannelMask(3),
        int32(AK_SPEAKER_FRONT_RIGHT));
    TestFalse(TEXT("Remaining members paused"), Reduced.HasPlaybackRequest());
    TestEqual(TEXT("One stereo copy"), Reduced.GetGain(), 1.0f);

    const FRadioChannelLayout Mono(Sources, Song, false);
    TestEqual(
        TEXT("Mono uses center channel"), Mono.GetChannelMask(3), int32(AK_SPEAKER_FRONT_CENTER));
    TestTrue(TEXT("Equal power for two mono members"),
        FMath::IsNearlyEqual(Mono.GetGain(), FMath::InvSqrt(2.0f)));
    return true;
}

#endif
