#include "Components/CharacterAudio/CharacterAudioDiagnosticUtils.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Data/Audio/WandererAudioActionsAsset.h"
#include "Data/Audio/WandererCharacterAudioTypes.h"

void CharacterAudioDiagnosticUtils::ReportRequest(const UObject* InContext,
    FName InAction,
    const FWandererCharacterAudioContext& InValues,
    EWandererAudioPlaybackScope InScope)
{
    WANDERER_DIAGNOSTIC(InContext,
        EDiagnosticDomain::Audio,
        TEXT("audio-request-received"),
        false,
        FString::Printf(TEXT("action=%s scope=%d surface=%s wetness=%s gait=%s"),
            *InAction.ToString(),
            static_cast<int32>(InScope),
            *InValues.Surface.ToString(),
            *InValues.Wetness.ToString(),
            *InValues.Gait.ToString()));
}

FWandererAudioExecutionResult CharacterAudioDiagnosticUtils::ReportResult(
    const UObject* InContext, FName InAction, const FWandererAudioExecutionResult& InResult)
{
    const bool bProblem = InResult.Status == EWandererAudioExecutionStatus::Failed
                          || InResult.Status == EWandererAudioExecutionStatus::PartialFailure;
    WANDERER_DIAGNOSTIC(InContext,
        EDiagnosticDomain::Audio,
        TEXT("audio-decision"),
        bProblem,
        FString::Printf(TEXT("action=%s status=%d accepted=%d playingID=%lld reason=%s"),
            *InAction.ToString(),
            static_cast<int32>(InResult.Status),
            InResult.AcceptedAudioActions,
            InResult.PlayingID,
            *InResult.Message));
    return InResult;
}
