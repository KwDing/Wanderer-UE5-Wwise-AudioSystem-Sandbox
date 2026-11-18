#pragma once

#include "AkComponent.h"

#include "AudioPooledEmitterComponent.generated.h"

UCLASS()
class UAudioPooledEmitterComponent : public UAkComponent
{
    GENERATED_BODY()

public:
    bool EnsureWwiseRegistration();
    void SuspendWwiseRegistration();
    void ClearReverbCache();
    void ApplyEmitterReverb(bool bInUseVolumes);
};
