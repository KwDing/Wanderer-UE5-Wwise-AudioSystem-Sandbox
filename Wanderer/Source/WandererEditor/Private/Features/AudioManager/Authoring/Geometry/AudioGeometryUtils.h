#pragma once

#include "Features/AudioManager/Authoring/Geometry/AudioGeometryTypes.h"

namespace AudioGeometryUtils
{
    FString Read(AActor* InActor, FAudioGeometrySnapshot& OutGeometry);
    /** Caller records Modify and validates all callback boundaries before these native writes. */
    bool Write(AActor* InTarget, const FAudioGeometrySnapshot& InGeometry);
    FString ValidateResult(AActor* InActor);
    bool MatchesGeometry(const FAudioGeometrySnapshot& InLeft,
                         const FAudioGeometrySnapshot& InRight);
    FString Digest(FAudioGeometrySnapshot InGeometry);
    FAudioGeometryMetadata ReadMetadata(const UWandererAudioSourceComponent& InAudio);
    void WriteMetadata(UWandererAudioSourceComponent& InAudio,
                       const FAudioGeometryMetadata& InMetadata);
    bool Matches(const FAudioGeometryMetadata& InLeft, const FAudioGeometryMetadata& InRight);
    void SetLabel(AActor& InTarget, const AActor& InSource, bool bInArea);
} // namespace AudioGeometryUtils
