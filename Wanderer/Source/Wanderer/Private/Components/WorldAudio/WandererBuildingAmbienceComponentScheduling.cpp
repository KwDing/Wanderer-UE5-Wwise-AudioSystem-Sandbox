#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"

#include "Engine/World.h"

#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Core/ActorPathUtils.h"

bool UWandererBuildingAmbienceComponent::CollectAudioRoomPaths(
    TArray<FSoftObjectPath>& OutPaths) const
{
    OutPaths.Reset();
    for (const TSoftObjectPtr<AActor>& Member : MemberRooms)
    {
        const AActor* Actor = Member.Get();
        const AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(Actor);
        if (Actor && (!Room || !Room->Room || Room->Room->AutoPost))
        {
            UE_LOG(LogWandererAudio,
                Error,
                TEXT("Building ambience invalid Room or Auto Post conflict: %s"),
                *GetNameSafe(Actor));
            return false;
        }
    }

    for (const TSoftObjectPtr<AActor>& Member : MemberRooms)
    {
        const FSoftObjectPath Path = ActorPathUtils::ForWorld(Member.ToSoftObjectPath(), GetWorld());
        if (!Path.IsNull())
        {
            OutPaths.AddUnique(Path);
        }
    }

    return true;
}
