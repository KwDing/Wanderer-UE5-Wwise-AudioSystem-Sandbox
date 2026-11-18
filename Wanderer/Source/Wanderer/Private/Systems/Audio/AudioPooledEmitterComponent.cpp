#include "Systems/Audio/AudioPooledEmitterComponent.h"

bool UAudioPooledEmitterComponent::EnsureWwiseRegistration()
{
    if (!IsRegisteredWithWwise())
    {
        RegisterGameObject();
        if (IsRegisteredWithWwise())
        {
            UpdateGameObjectPosition();
        }
    }
    return IsRegisteredWithWwise();
}

void UAudioPooledEmitterComponent::SuspendWwiseRegistration()
{
    if (IsRegisteredWithWwise())
    {
        UnregisterGameObject();
    }
    CurrentRoom.Reset();
}

void UAudioPooledEmitterComponent::ClearReverbCache()
{
    ReverbFadeControls.Reset();
    CurrentAuxSendValues.Reset();
}

void UAudioPooledEmitterComponent::ApplyEmitterReverb(bool bInUseVolumes)
{
    bUseReverbVolumes = bInUseVolumes;
    if (!bUseReverbVolumes)
    {
        return;
    }
    UpdateAkLateReverbComponentList(GetComponentLocation());
    for (AkReverbFadeControl& Control : ReverbFadeControls)
    {
        Control.ForceCurrentToTargetValue();
    }
    ApplyAkReverbVolumeList(0);
}
