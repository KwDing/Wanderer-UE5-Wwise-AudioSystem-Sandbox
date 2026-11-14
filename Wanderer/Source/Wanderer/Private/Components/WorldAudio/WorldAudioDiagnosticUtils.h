#pragma once

#include "CoreMinimal.h"

class UWandererAudioSourceComponent;

namespace WorldAudioDiagnosticUtils
{
    void ReportResourceFailure(
        const UWandererAudioSourceComponent* InSource, const FSoftObjectPath& InEvent);
    void ReportState(const UWandererAudioSourceComponent* InSource,
        bool bInAdmitted,
        FName InReason,
        bool bInNight);
} // namespace WorldAudioDiagnosticUtils
