#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "AkSpatialAudioVolume.h"
#include "Interfaces/Audio/AudioGeometryProvider.h"

// No emitter creation or media/playback changes; queries may rebuild geometry caches.
bool UWandererAudioSourceComponent::EvaluateGeometry(
    const FVector& Listener, const FWandererWorldAudioProfileRow& Row, bool& bInsideRoom)
{
    bInsideRoom = false;
    DistanceM = FVector::Distance(Listener, GetOwner()->GetActorLocation()) / 100.0f;
    if (Row.Mode == EWandererWorldAudioMode::Global)
    {
        DistanceM = 0;
    }
    if (Row.Mode == EWandererWorldAudioMode::Spline || Row.Mode == EWandererWorldAudioMode::Area)
    {
        const IAudioGeometryProvider* Shape = Cast<IAudioGeometryProvider>(GetOwner());
        if (!Shape)
        {
            Status = TEXT("Audio geometry provider unavailable");
            return false;
        }
        DistanceM = Shape->GetAudioBoundsDistanceM(Listener);
        if (DistanceM > Row.ReleaseDistanceM)
        {
            Status = TEXT("Geometry bounds culled");
            return false;
        }
        FAudioGeometrySample Sample;
        if (!Shape->QueryAudioGeometry(Listener, Row.SplineBlendDistanceM, Sample))
        {
            Status = TEXT("Audio geometry query failed");
            return false;
        }
        AreaEmitterPosition = Sample.Position;
        DistanceM = Sample.DistanceM;
        InsideBlend = Sample.InsideBlend;
    }
    if (const auto* Room = Row.Mode == EWandererWorldAudioMode::Room ? ResolveRoom() : nullptr)
    {
        float DistanceCM = 0;
        bInsideRoom = Room->EncompassesPoint(Listener, 0, &DistanceCM);
        DistanceM = bInsideRoom ? 0 : FMath::Max(0.0f, DistanceCM / 100.0f);
    }
    return true;
}
