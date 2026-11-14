#include "Actors/Audio/WandererAudioSplineEmitter.h"

EAudioGeometryKind AWandererAudioSplineEmitter::GetAudioGeometryKind() const
{
    return EAudioGeometryKind::Spline;
}

FString AWandererAudioSplineEmitter::GetAudioGeometryError() const
{
    return GetGeometryError();
}

float AWandererAudioSplineEmitter::GetAudioBoundsDistanceM(const FVector& InListener) const
{
    return GetBoundsDistanceM(InListener);
}

bool AWandererAudioSplineEmitter::QueryAudioGeometry(
    const FVector& InListener, float InBlendDistanceM, FAudioGeometrySample& OutSample) const
{
    return QueryListener(InListener,
        InBlendDistanceM,
        OutSample.Position,
        OutSample.DistanceM,
        OutSample.InsideBlend);
}
