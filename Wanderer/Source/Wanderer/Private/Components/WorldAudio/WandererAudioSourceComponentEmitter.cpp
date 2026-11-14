#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

UAkGameObject* UWandererAudioSourceComponent::GetPlaybackObject()
{
    const auto* Row = GetProfile();
    if (!Row)
    {
        return nullptr;
    }
    if (Row->Mode == EWandererWorldAudioMode::Room)
    {
        auto* Room = ResolveRoom();
        return Room && Room->Room && Room->Room->bEnable ? Room->Room.Get() : nullptr;
    }
    CreateOwnedEmitter();
    UpdateEmitterGeometry(*Row);
    UpdateSplineBlend(*Row);
    UpdateAreaPresence(Row->Mode == EWandererWorldAudioMode::Area);
    return OwnedEmitter;
}

void UWandererAudioSourceComponent::CreateOwnedEmitter()
{
    if (!OwnedEmitter && HasBegunPlay() && IsValid(GetOwner()))
    {
        // Dedicated component: never borrow or stop an unrelated object's sound.
        OwnedEmitter = NewObject<UAkComponent>(GetOwner(), NAME_None, RF_Transient);
        AppliedAreaRadius = -1;
        AppliedInsideBlend = -1;
        AppliedAreaPresence = -1;
        OwnedEmitter->SetupAttachment(GetOwner()->GetRootComponent());
        OwnedEmitter->OcclusionRefreshInterval = 0;
        OwnedEmitter->StopWhenOwnerDestroyed = true;
        OwnedEmitter->RegisterComponent();
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
            OwnedEmitter->SetGameObjectRadius(Radius, FMath::Max(.001f, Radius * .05f));
            AppliedAreaRadius = Radius;
        }
    }
    else if (OwnedEmitter && AppliedAreaRadius >= 0)
    {
        OwnedEmitter->SetGameObjectRadius(0, 0);
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
            OwnedEmitter->SetRTPCValue(nullptr,
                InsideBlend,
                AppliedInsideBlend < 0 ? 0 : FMath::RoundToInt(Row.SplineBlendSeconds * 1000),
                TEXT("AmbientSpline_Inside"));
            AppliedInsideBlend = InsideBlend;
        }
    }
    else if (OwnedEmitter && AppliedInsideBlend >= 0)
    {
        OwnedEmitter->SetRTPCValue(nullptr, 0, 0, TEXT("AmbientSpline_Inside"));
        AppliedInsideBlend = -1;
    }
}
