#include "Actors/Audio/WandererAudioAreaEmitter.h"

EAudioGeometryKind AWandererAudioAreaEmitter::GetAudioGeometryKind() const
{
    return EAudioGeometryKind::Area;
}

FString AWandererAudioAreaEmitter::GetAudioGeometryError() const
{
    return IsShapeValid() ? FString() : TEXT("Area geometry is invalid");
}

float AWandererAudioAreaEmitter::GetAudioBoundsDistanceM(const FVector& InListener) const
{
    return GetBoundsDistanceM(InListener);
}

bool AWandererAudioAreaEmitter::QueryAudioGeometry(
    const FVector& InListener, float InBlendDistanceM, FAudioGeometrySample& OutSample) const
{
    float DistanceCM = 0.0f;
    if (!FindClosestPoint(InListener, OutSample.Position, DistanceCM))
    {
        return false;
    }
    constexpr float CentimetersPerMeter = 100.0f;
    OutSample.DistanceM = DistanceCM / CentimetersPerMeter;
    // Baked-cell containment must not depend on the emitter's spread radius.
    OutSample.InsideBlend = DistanceCM <= UE_KINDA_SMALL_NUMBER ? 1.0f : 0.0f;
    return true;
}
