#pragma once

#include "CoreMinimal.h"

class UObject;
struct FWandererAudioExecutionResult;
struct FWandererCharacterAudioContext;
enum class EWandererAudioPlaybackScope : uint8;

namespace CharacterAudioDiagnosticUtils
{
    void ReportRequest(const UObject* InContext,
        FName InAction,
        const FWandererCharacterAudioContext& InValues,
        EWandererAudioPlaybackScope InScope);
    FWandererAudioExecutionResult ReportResult(
        const UObject* InContext, FName InAction, const FWandererAudioExecutionResult& InResult);
} // namespace CharacterAudioDiagnosticUtils
