#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/Audio/AudioSchedulingTypes.h"

#include "AudioEventLeaseManager.generated.h"

class UAkAudioEvent;
class UAkGameObject;

/**
 * Owns this world's manual Event leases; process accounting prevents one PIE world
 * from unloading media still leased by another. Auto Load remains Integration-owned.
 * Game-thread only. The subsystem must stop playback before Stop; Stop is terminal.
 */
UCLASS()
class UAudioEventLeaseManager : public UObject
{
    GENERATED_BODY()

  public:
    bool Acquire(UAkAudioEvent* InEvent);
    EAudioEventResourceStatus Advance(UAkAudioEvent* InEvent, float InPriority = 100.0f);
    static FString GetLoadSummary();
    static bool IsReady(UAkAudioEvent* InEvent);
    static uint32 Post(UAkAudioEvent& InEvent, UAkGameObject* InObject = nullptr);
    void Release(UAkAudioEvent* InEvent);
    void Retire(UAkAudioEvent* InEvent, uint32 InPlayingID, double InTailDeadlineSec, float InTailSec);
    void Stop();
    int32 GetEventCount() const;

  private:
    friend struct FAudioPriorityTestAccess;
    static uint64 GetRequestOrder(UAkAudioEvent* InEvent);

    UPROPERTY(Transient)
    TMap<TObjectPtr<UAkAudioEvent>, int32> EventUsers;
    bool bStopped = false;
};
