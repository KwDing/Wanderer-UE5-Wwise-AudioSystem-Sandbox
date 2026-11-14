#include "Actors/WandererSpeakerActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Templates/UnrealTemplate.h"

#include "Data/WandererSpeakerSettings.h"

AWandererSpeakerActor::AWandererSpeakerActor()
{
    PlacementBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlacementBox"));
    SetRootComponent(PlacementBox);
    PlacementBox->SetCollisionObjectType(ECC_WorldDynamic);
    PlacementBox->SetCollisionResponseToAllChannels(ECR_Block);
    PlacementBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(PlacementBox);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetLightingChannels(true, false, true);
    EmitterAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("EmitterAnchor"));
    EmitterAnchor->SetupAttachment(PlacementBox);
}

void AWandererSpeakerActor::RequestPlayback(FName NewOptionID, bool bRequested)
{
    if (bEndingPlay || bPublishingPlayback || IsActorBeingDestroyed()
        || (OptionID == NewOptionID && bPlaybackRequested == bRequested))
    {
        return;
    }
    OptionID = NewOptionID;
    bPlaybackRequested = bRequested;
    {
        TGuardValue<bool> Guard(bPublishingPlayback, true);
        OnPlaybackRequested.Broadcast(OptionID, bPlaybackRequested);
    }
    // A listener may destroy this actor. Deliver its final state after the active multicast.
    if (bEndingPlay)
    {
        BroadcastStopped();
    }
}

bool AWandererSpeakerActor::ConfigureVisual(UStaticMesh* InMesh, float InLongestEdge)
{
    if (!InMesh || !FMath::IsFinite(InLongestEdge) || InLongestEdge <= 0)
    {
        return false;
    }
    const FBoxSphereBounds Bounds = InMesh->GetBounds();
    const double Edge = Bounds.BoxExtent.GetMax() * 2;
    if (Edge <= UE_SMALL_NUMBER)
    {
        return false;
    }
    const double Scale = InLongestEdge / Edge;
    // Vendor meshes face +Y; the gameplay anchor uses +X as the emitting/front direction.
    const FRotator Facing(0, -90, 0);
    const FBoxSphereBounds OrientedBounds = Bounds.TransformBy(FTransform(Facing));
    Visual->SetStaticMesh(InMesh);
    Visual->SetRelativeRotation(Facing);
    Visual->SetRelativeScale3D(FVector(Scale));
    Visual->SetRelativeLocation(-OrientedBounds.Origin * Scale);
    PlacementBox->SetBoxExtent(OrientedBounds.BoxExtent * Scale);
    return true;
}

FVector AWandererSpeakerActor::GetPlacementExtent() const
{
    return PlacementBox->GetUnscaledBoxExtent();
}

void AWandererSpeakerActor::SetPreview(bool bInPreview)
{
    PlacementBox->SetCollisionEnabled(
        bInPreview ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    Visual->SetCastShadow(!bInPreview);
}

void AWandererSpeakerActor::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    bEndingPlay = true;
    const bool bWasPlaying = bPlaybackRequested;
    bPlaybackRequested = false;
    if (!bPublishingPlayback)
    {
        if (bWasPlaying)
        {
            BroadcastStopped();
        }
        OnPlaybackRequested.Clear();
    }
    Super::EndPlay(InEndPlayReason);
}

void AWandererSpeakerActor::BroadcastStopped()
{
    TGuardValue<bool> Guard(bPublishingPlayback, true);
    OnPlaybackRequested.Broadcast(OptionID, false);
    OnPlaybackRequested.Clear();
}
