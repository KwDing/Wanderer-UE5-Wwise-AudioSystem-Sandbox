#pragma once

#include "CoreMinimal.h"

#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Types/Audio/AudioEmitterTypes.h"

class AActor;
class UAkComponent;
struct FStreamableHandle;

struct FObjectAudioPlayback
{
    FObjectAudioEvent Definition;
    TSharedPtr<FStreamableHandle> LoadHandle;
    TWeakObjectPtr<UAkAudioEvent> LeaseWeakPtr;
    uint32 PlayingID = 0;
    bool bStopping = false;
    double RequestedAtSec = 0;
    double RetryAtSec = 0;
};

/** World-owned intent and weak attachment; leases and pending pool releases own native playback. */
struct FObjectAudioEmitterState
{
    FObjectAudioEmitterHandle Handle;
    TWeakObjectPtr<AActor> OwnerWeakPtr;
    FObjectAudioAttachment Attachment;
    TArray<FObjectAudioEvent> Events;
    TArray<FObjectAudioPlayback> Playbacks;
    TWeakObjectPtr<UAkComponent> EmitterWeakPtr;
    int32 LoopLimit = 0;
    int32 ShortEventLimit = 0;
    uint64 PlaybackRevision = 0;
    FName LastFailedEvent;
    EObjectAudioFailure LastFailure = EObjectAudioFailure::None;
    bool bRegistered = true;
    bool bAdmitted = false;
    bool bPreparing = false;
    bool bEmergency = false;
    float BasePriority = 60;
    float EffectivePriority = 0;
    float AppliedPriority = -1;
    float ActivationDistanceM = 35;
    float ReleaseDistanceM = 42;
    float FadeInSec = 0.35f;
    float FadeOutSec = 0.35f;
    FName ResourceGroup;
};
