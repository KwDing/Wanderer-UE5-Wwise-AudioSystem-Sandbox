#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "AkRoomComponent.h"

FAudioSchedulingInfo UWandererAudioSourceComponent::GetSchedulingInfo() const
{
    FAudioSchedulingInfo Info;
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (!Row)
    {
        return Info;
    }

    Info.ProfileKey = Profile.DataTable ? GetPathNameSafe(Profile.DataTable) + TEXT(":")
                                              + Profile.RowName.ToString()
                                        : GetPathName();
    Info.OwnerPath = GetPathNameSafe(GetOwner());
    Info.ResourceGroup = ResourceGroup;
    Info.Priority = GetEffectivePriority();
    Info.MaxInstances = Row->MaxInstances;
    Info.bProtected = IsProtected();
    Info.bPlaying = IsPlaying();
    Info.bAmbience = Row->Category == EWandererWorldAudioCategory::Ambience;
    Info.bDetail = Row->Category == EWandererWorldAudioCategory::Detail;
    return Info;
}

FName UWandererAudioSourceComponent::GetAudioResourceGroup() const
{
    return ResourceGroup;
}

const UObject* UWandererAudioSourceComponent::GetPlaybackIdentity() const
{
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (Row && Row->Mode == EWandererWorldAudioMode::Room)
    {
        return ResolvePlaybackRoom();
    }
    return Row ? this : nullptr;
}

bool UWandererAudioSourceComponent::CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const
{
    OutPaths.Reset();
    return true;
}
