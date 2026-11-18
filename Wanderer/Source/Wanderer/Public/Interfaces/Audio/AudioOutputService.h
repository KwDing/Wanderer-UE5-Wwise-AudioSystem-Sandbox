#pragma once

#include "CoreMinimal.h"

enum class EListenerMenuMode : uint8;

/** Process-wide mix controls. Access from the game thread. */
namespace WandererAudioOutput
{
    WANDERER_API bool IsReady();
    WANDERER_API bool TrySetMenuMix(EListenerMenuMode InMode);
    WANDERER_API bool TrySetListenerUnderwater(bool bInUnderwater);
    WANDERER_API bool ReadHeadphoneOutput(bool& bOutEnabled);
    WANDERER_API bool WriteHeadphoneOutput(bool bInEnabled);
}
