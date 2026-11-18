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
#define WANDERER_AUDIO_CHANNEL_NAME_CASE(Name) \
    case EWandererAudioLogChannel::Name:       \
        return TEXT(#Name);

    switch (InChannel)
    {
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Configuration)
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Footsteps)
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Actions)
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Switches)
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Playback)
    WANDERER_AUDIO_CHANNEL_NAME_CASE(Amb)
    default:
        return TEXT("Unknown");
    }

#undef WANDERER_AUDIO_CHANNEL_NAME_CASE
}
