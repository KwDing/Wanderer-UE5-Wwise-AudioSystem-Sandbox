#pragma once

#include "CoreMinimal.h"

enum class EListenerMenuMode : uint8
{
    Closed,
    Focused,
    Paused
};

struct FListenerWaterSample
{
    bool bHasSurface = false;
    float DepthCm = 0.0f;
    float HysteresisCm = 5.0f;
    float HoldSec = 0.25f;
    float TransitionIntervalMinSec = 0.5f;
    double SampleTimeSec = 0.0;
};
