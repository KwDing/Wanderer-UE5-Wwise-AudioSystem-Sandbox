#pragma once

#include "UObject/Interface.h"

#include "Types/Audio/AudioGeometryTypes.h"

#include "AudioGeometryProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAudioGeometryProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Host-owned shape queries, independent of playback and media residency.
 * Call synchronously on the game thread; queries may rebuild geometry caches.
 */
class WANDERER_API IAudioGeometryProvider
{
    GENERATED_BODY()

  public:
    virtual EAudioGeometryKind GetAudioGeometryKind() const = 0;
    virtual FString GetAudioGeometryError() const = 0;
    virtual float GetAudioBoundsDistanceM(const FVector& InListener) const = 0;
    virtual bool QueryAudioGeometry(const FVector& InListener,
        float InBlendDistanceM,
        FAudioGeometrySample& OutSample) const = 0;
};
