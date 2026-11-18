#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Interfaces/Audio/AudioSourceInterface.h"

#include "AudioSchedulingTestSource.generated.h"

/** Test-only alternative source: deliberately unrelated to the production source component. */
UCLASS()
class UAudioSchedulingTestSource : public UObject, public IAudioSourceInterface
{
    GENERATED_BODY()

  public:
    UPROPERTY()
    TObjectPtr<UObject> PlaybackIdentity;

    FAudioSchedulingInfo Info;
    TArray<FSoftObjectPath> RoomPaths;
    FName LastReason;
    int32 PlayingID = 0;
    int32 ShutdownCount = 0;
    bool bEligible = true;
    bool bStopDuringEvaluation = false;
    bool bStopDuringUpdate = false;
    int32 UpdateCount = 0;
    bool bAdmitted = false;
    bool bNight = false;
    bool bMediaReady = true;

    virtual UWorld* GetWorld() const override;
    virtual bool Evaluate(const FVector& InListener) override;
    virtual FAudioSchedulingInfo GetSchedulingInfo() const override;
    virtual FName GetAudioResourceGroup() const override;
    virtual int32 GetPlayingID() const override;
    virtual const UObject* GetPlaybackIdentity() const override;
    virtual bool CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const override;
    virtual bool PrepareForAdmission(bool bInNight) override;
    virtual void UpdatePlayback(bool bInAdmitted, FName InReason, bool bInNight) override;
    virtual void ShutdownPlayback() override;
};
