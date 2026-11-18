#include "Tests/AudioRuntime/AudioSchedulingTestSource.h"

#include "Engine/World.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

UWorld* UAudioSchedulingTestSource::GetWorld() const
{
    return GetTypedOuter<UWorld>();
}

bool UAudioSchedulingTestSource::Evaluate(const FVector& InListener)
{
    if (bStopDuringEvaluation)
    {
        GetWorld()->GetSubsystem<UWandererAudioSubsystem>()->Deinitialize();
    }
    return bEligible;
}

FAudioSchedulingInfo UAudioSchedulingTestSource::GetSchedulingInfo() const
{
    return Info;
}

FName UAudioSchedulingTestSource::GetAudioResourceGroup() const
{
    return Info.ResourceGroup;
}

int32 UAudioSchedulingTestSource::GetPlayingID() const
{
    return PlayingID;
}

const UObject* UAudioSchedulingTestSource::GetPlaybackIdentity() const
{
    return PlaybackIdentity;
}

bool UAudioSchedulingTestSource::CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const
{
    OutPaths = RoomPaths;
    return true;
}

bool UAudioSchedulingTestSource::PrepareForAdmission(bool bInNight)
{
    return bMediaReady;
}

void UAudioSchedulingTestSource::UpdatePlayback(bool bInAdmitted, FName InReason, bool bInNight)
{
    ++UpdateCount;
    bAdmitted = bInAdmitted;
    LastReason = InReason;
    bNight = bInNight;
    if (bStopDuringUpdate)
    {
        GetWorld()->GetSubsystem<UWandererAudioSubsystem>()->Deinitialize();
    }
}

void UAudioSchedulingTestSource::ShutdownPlayback()
{
    ++ShutdownCount;
    bAdmitted = false;
}
