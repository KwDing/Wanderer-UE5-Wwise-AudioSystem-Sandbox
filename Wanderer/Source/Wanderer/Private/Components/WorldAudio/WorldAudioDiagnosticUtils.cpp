#include "Components/WorldAudio/WorldAudioDiagnosticUtils.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Core/Diagnostics/DiagnosticRecorder.h"

void WorldAudioDiagnosticUtils::ReportResourceFailure(
    const UWandererAudioSourceComponent* InSource, const FSoftObjectPath& InEvent)
{
    WANDERER_DIAGNOSTIC(InSource,
        EDiagnosticDomain::Audio,
        TEXT("resource-failure"),
        true,
        FString::Printf(
            TEXT("state=%s requestedEvent=%s"), *InSource->GetStatus(), *InEvent.ToString()));
}

void WorldAudioDiagnosticUtils::ReportState(
    const UWandererAudioSourceComponent* InSource, bool bInAdmitted, FName InReason, bool bInNight)
{
#if !UE_BUILD_SHIPPING
    if (!FDiagnosticRecorder::IsEnabled(EDiagnosticDomain::Audio))
    {
        return;
    }
    const FString State = InSource->GetStatus();
    if (!FDiagnosticRecorder::HasTransition(InSource, State)
        || !FDiagnosticRecorder::Admit(InSource, EDiagnosticDomain::Audio, false))
    {
        return;
    }
    FDiagnosticRecorder::Record(InSource,
        EDiagnosticDomain::Audio,
        TEXT("schedule-state"),
        FString::Printf(TEXT("admitted=%d reason=%s state=%s profile=%s group=%s "
                             "distanceM=%.2f priority=%.2f protected=%d night=%d playingID=%d"),
            bInAdmitted,
            *InReason.ToString(),
            *State,
            *InSource->Profile.RowName.ToString(),
            *InSource->ResourceGroup.ToString(),
            InSource->GetDistanceM(),
            InSource->GetEffectivePriority(),
            InSource->IsProtected(),
            bInNight,
            InSource->GetPlayingID()));
    FDiagnosticRecorder::CommitTransition(InSource, State);
#endif
}
