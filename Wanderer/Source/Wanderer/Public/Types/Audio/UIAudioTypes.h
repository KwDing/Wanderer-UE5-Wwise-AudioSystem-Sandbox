#pragma once

#include "CoreMinimal.h"
#include "UIAudioTypes.generated.h"

UENUM(BlueprintType)
enum class EWandererUIAudioAction : uint8
{
    Open,
    Choose,
    Cancel,
    Close,
    Silent
};
