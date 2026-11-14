#include "Systems/Audio/WandererAudioDiagnostics.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"

bool FWandererAudioDiagnostics::IsMasterEnabled()
{
    return FDiagnosticRecorder::GetPreset() != 0;
}

void FWandererAudioDiagnostics::SetMasterEnabled(bool bEnabled)
{
    FDiagnosticRecorder::SetPreset(bEnabled ? 1 : 0);
}

bool FWandererAudioDiagnostics::IsChannelEnabled(EWandererAudioLogChannel Channel)
{
    return Channel != EWandererAudioLogChannel::Footsteps
           || FDiagnosticRecorder::IsFootstepsEnabled();
}

void FWandererAudioDiagnostics::SetChannelEnabled(EWandererAudioLogChannel Channel, bool bEnabled)
{
    // Legacy callers retain source compatibility; presets replace independent channel switches.
    if (Channel == EWandererAudioLogChannel::Footsteps)
    {
        FDiagnosticRecorder::SetFootstepsEnabled(bEnabled);
    }
}

bool FWandererAudioDiagnostics::IsEnabled(EWandererAudioLogChannel Channel)
{
    return IsChannelEnabled(Channel) && FDiagnosticRecorder::IsEnabled(EDiagnosticDomain::Audio);
}

void FWandererAudioDiagnostics::Publish(const UObject* Context,
    EWandererAudioLogChannel Channel,
    FName Level,
    const FString& Text,
    FName Source)
{
    if (!IsInGameThread() || !IsChannelEnabled(Channel))
    {
        return;
    }
    const bool bProblem = Level == TEXT("Error") || Level == TEXT("Warning");
    WANDERER_DIAGNOSTIC(Context,
        EDiagnosticDomain::Audio,
        TEXT("legacy-detail"),
        bProblem,
        FString::Printf(
            TEXT("channel=%s source=%s %s"), GetChannelName(Channel), *Source.ToString(), *Text));
}

const TCHAR* FWandererAudioDiagnostics::GetChannelName(EWandererAudioLogChannel InChannel)
{
    switch (InChannel)
    {
    case EWandererAudioLogChannel::Configuration:
        return TEXT("Configuration");
    case EWandererAudioLogChannel::Footsteps:
        return TEXT("Footsteps");
    case EWandererAudioLogChannel::Actions:
        return TEXT("Actions");
    case EWandererAudioLogChannel::Switches:
        return TEXT("Switches");
    case EWandererAudioLogChannel::Playback:
        return TEXT("Playback");
    case EWandererAudioLogChannel::Amb:
        return TEXT("Amb");
    default:
        return TEXT("Unknown");
    }
}
