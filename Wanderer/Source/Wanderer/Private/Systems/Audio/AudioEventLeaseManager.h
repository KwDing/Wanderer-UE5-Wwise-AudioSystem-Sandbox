#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "AudioEventLeaseManager.generated.h"

class UAkAudioEvent;

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
    void Release(UAkAudioEvent* InEvent);
    void Stop();
    int32 GetEventCount() const;

  private:
    UPROPERTY(Transient)
    TMap<TObjectPtr<UAkAudioEvent>, int32> EventUsers;
    bool bStopped = false;
};
