#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "Engine/World.h"

#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

UAkGameObject* UWandererAudioSourceComponent::GetPlaybackObject() const
{
    const auto* Row = GetProfile();
    if (!Row)
    {
        return nullptr;
    }
    if (Row->Mode == EWandererWorldAudioMode::Room)
    {
        return ResolvePlaybackRoom();
    }
    return OwnedEmitter;
}

UAkGameObject* UWandererAudioSourceComponent::AcquirePlaybackObject()
{
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (Row && Row->Mode != EWandererWorldAudioMode::Room)
    {
        if (UAkComponent* Emitter = AcquireWorldEmitter(false))
        {
            Emitter->bUseReverbVolumes = true;
            if (USceneComponent* Root = GetOwner()->GetRootComponent())
            {
                Emitter->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            }
            else
            {
                Emitter->SetWorldTransform(GetOwner()->GetActorTransform());
            }
        }
        UpdateOwnedEmitter();
    }
    return GetPlaybackObject();
}

UAkRoomComponent* UWandererAudioSourceComponent::ResolvePlaybackRoom() const
{
    const AAkSpatialAudioVolume* Room = ResolveRoom();
    return Room && IsValid(Room->Room) && Room->Room->bEnable ? Room->Room.Get() : nullptr;
}

UAkComponent* UWandererAudioSourceComponent::AcquireWorldEmitter(bool bInPreferPrevious)
{
    UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    if (!OwnedEmitter && HasBegunPlay() && IsValid(GetOwner()) && System)
    {
        OwnedEmitter = System->AcquireEmitter(*this, bInPreferPrevious);
        bPreferPreviousEmitter = bInPreferPrevious;
        AppliedAreaRadius = -1;
        AppliedInsideBlend = -1;
        AppliedAreaPresence = -1;
    }
    return OwnedEmitter;
}

void UWandererAudioSourceComponent::UpdateOwnedEmitter()
{
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (OwnedEmitter && Row)
    {
        UpdateEmitterGeometry(*Row);
        UpdateSplineBlend(*Row);
        UpdateAreaPresence(Row->Mode == EWandererWorldAudioMode::Area);
    }
}

void UWandererAudioSourceComponent::UpdateEmitterGeometry(const FWandererWorldAudioProfileRow& Row)
{
    if (OwnedEmitter
        && (Row.Mode == EWandererWorldAudioMode::Area
            || Row.Mode == EWandererWorldAudioMode::Spline))
    {
        OwnedEmitter->SetWorldLocation(AreaEmitterPosition);
        const float Radius = Row.AreaSpreadRadiusM * 100;
        if (!FMath::IsNearlyEqual(AppliedAreaRadius, Radius))
        {
            WandererAudioPlayback::SetEmitterRadius(*OwnedEmitter, Radius, FMath::Max(.001f, Radius * .05f));
            AppliedAreaRadius = Radius;
        }
    }
    else if (OwnedEmitter && AppliedAreaRadius >= 0)
    {
        WandererAudioPlayback::SetEmitterRadius(*OwnedEmitter, 0, 0);
        OwnedEmitter->SetRelativeLocation(FVector::ZeroVector);
        AppliedAreaRadius = -1;
    }
}

void UWandererAudioSourceComponent::UpdateSplineBlend(const FWandererWorldAudioProfileRow& Row)
{
    if (OwnedEmitter && Row.Mode == EWandererWorldAudioMode::Spline)
    {
        if (!FMath::IsNearlyEqual(AppliedInsideBlend, InsideBlend, .001f))
        {
            WandererAudioPlayback::SetWorldEmitterParameter(*OwnedEmitter, EWorldAudioEmitterParameter::SplineInside,
                InsideBlend, AppliedInsideBlend < 0 ? 0 : FMath::RoundToInt(Row.SplineBlendSeconds * 1000));
            AppliedInsideBlend = InsideBlend;
        }
    }
    else if (OwnedEmitter && AppliedInsideBlend >= 0)
    {
        WandererAudioPlayback::SetWorldEmitterParameter(*OwnedEmitter, EWorldAudioEmitterParameter::SplineInside, 0, 0);
        AppliedInsideBlend = -1;
    }
}
