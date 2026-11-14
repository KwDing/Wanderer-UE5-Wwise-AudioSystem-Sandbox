#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Types/Audio/AudioSchedulingTypes.h"

#include "AudioSourceInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAudioSourceInterface : public UInterface
{
    GENERATED_BODY()
};

/** Synchronous game-thread contract. Calls must not trigger GC or destroy other sources. */
class WANDERER_API IAudioSourceInterface
{
    GENERATED_BODY()

  public:
    virtual bool Evaluate(const FVector& InListener) = 0;
    virtual FAudioSchedulingInfo GetSchedulingInfo() const = 0;
    virtual FName GetAudioResourceGroup() const = 0;
    virtual int32 GetPlayingID() const = 0;
    virtual UObject* GetPlaybackIdentity() = 0;
    virtual bool CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const = 0;
    virtual void UpdatePlayback(bool bInAdmitted, FName InReason, bool bInNight) = 0;
    virtual void ShutdownPlayback() = 0;
};
