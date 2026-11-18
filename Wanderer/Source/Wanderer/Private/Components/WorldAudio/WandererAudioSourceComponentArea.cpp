#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "AkComponent.h"

#include "Interfaces/Audio/AudioPlaybackService.h"

void UWandererAudioSourceComponent::UpdateAreaPresence(bool bArea)
{
    if (!OwnedEmitter || (!bArea && AppliedAreaPresence < 0))
    {
        return;
    }
    constexpr int32 EntryFadeMs = 500;
    constexpr auto Parameter = EWorldAudioEmitterParameter::AreaPresence;
    const float Target = bArea ? InsideBlend : 0.0f;
    if (AppliedAreaPresence < 0)
    {
        // Initialize before Post, including spawning/loading while already inside.
        WandererAudioPlayback::SetWorldEmitterParameter(*OwnedEmitter, Parameter, 0, 0);
        AppliedAreaPresence = 0;
    }
    if (AppliedAreaPresence == Target)
    {
        return;
    }
    // Only sounds mapped to this parameter are confined. Exiting never spills a fade outside.
    WandererAudioPlayback::SetWorldEmitterParameter(*OwnedEmitter, Parameter, Target, Target > 0 ? EntryFadeMs : 0);
    AppliedAreaPresence = Target;
}
