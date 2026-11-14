#pragma once

#include "Data/Audio/WandererCharacterAudioTypes.h"

namespace WandererCharacterActions
{
    using FSurfaceValues = TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<UAkSwitchValue>>;

    const TArray<FWandererAudioAction>* GetAudioActions(
        const FWandererCharacterAudioActionRow& Row);

    bool IsParameterAudioAction(const FWandererAudioAction& AudioAction);

} // namespace WandererCharacterActions
