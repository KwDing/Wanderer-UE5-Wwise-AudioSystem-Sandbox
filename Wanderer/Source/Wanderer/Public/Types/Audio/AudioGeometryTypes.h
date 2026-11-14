#pragma once

#include "CoreMinimal.h"

enum class EAudioGeometryKind : uint8
{
    Area,
    Spline
};

struct FAudioGeometrySample
{
    FVector Position = FVector::ZeroVector;
    float DistanceM = 0.0f;
    float InsideBlend = 0.0f;
};
