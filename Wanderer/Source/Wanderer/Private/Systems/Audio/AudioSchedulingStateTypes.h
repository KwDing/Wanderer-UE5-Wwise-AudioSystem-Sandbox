#pragma once

#include "CoreMinimal.h"

#include "Types/Audio/AudioSchedulingTypes.h"

class UAkAudioEvent;
struct FStreamableHandle;

/** Private state owned by the world subsystem; never a public resource configuration. */
struct FWandererAudioResourceState
{
    enum class EPhase : uint8
    {
        Dormant,
        Loading,
        Active,
        Draining
    };

    EPhase Phase = EPhase::Dormant;
    TSharedPtr<FStreamableHandle> LoadHandle;
    TArray<TWeakObjectPtr<UAkAudioEvent>> Leases;
    double OutsideSinceSec = -1.0;
    double LoadStartedAtSec = 0.0;
    double RetryAtSec = 0.0;
    float DistanceM = 0.0f;
    FString Error;
};

/** Ephemeral admission record. The subsystem observes, but never owns, the source. */
struct FAudioSourceCandidate
{
    TWeakObjectPtr<UObject> SourceWeakPtr;
    FAudioSchedulingInfo Info;
};

struct FAudioResourceStateMap
{
    TMap<FName, FWandererAudioResourceState> Groups;
};
