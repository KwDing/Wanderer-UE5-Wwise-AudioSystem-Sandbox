#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "AkComponent.h"

void UWandererAudioSourceComponent::UpdateAreaPresence(bool bArea)
{
    if (!OwnedEmitter || (!bArea && AppliedAreaPresence < 0))
    {
        return;
    }
    constexpr int32 EntryFadeMs = 500;
    const TCHAR* Parameter = TEXT("AmbientArea_Inside");
    const float Target = bArea ? InsideBlend : 0.0f;
    if (AppliedAreaPresence < 0)
    {
        // Initialize before Post, including spawning/loading while already inside.
        OwnedEmitter->SetRTPCValue(nullptr, 0, 0, Parameter);
        AppliedAreaPresence = 0;
    }
    if (AppliedAreaPresence == Target)
    {
        return;
    }
    // Only sounds mapped to this parameter are confined. Exiting never spills a fade outside.
    OwnedEmitter->SetRTPCValue(nullptr, Target, Target > 0 ? EntryFadeMs : 0, Parameter);
    AppliedAreaPresence = Target;
}
