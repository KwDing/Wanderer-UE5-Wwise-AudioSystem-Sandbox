#include "Components/Audio/WandererObjectAudioComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

bool UWandererObjectAudioComponent::ActivateEmitter(FGuid InInstanceID, USceneComponent* InTarget,
    bool bInEquipped, FTransform InRelativeTransform)
{
    UWorld* World = GetWorld();
    UWandererAudioSubsystem* Audio = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;
    if (!Audio || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
    {
        return false;
    }
    if (EmitterHandle.InstanceID == InInstanceID)
    {
        FTransform CurrentTransform;
        if (Audio->ResolveObjectEmitter(EmitterHandle, CurrentTransform))
        {
            return UpdateAttachment(InTarget, bInEquipped, InRelativeTransform);
        }
    }
    DeactivateEmitter();
    EmitterHandle = Audio->RegisterObjectEmitter(InInstanceID, *GetOwner(),
        MakeAttachment(InTarget, bInEquipped, InRelativeTransform), Profile);
    AudioWeakPtr = Audio;
    return EmitterHandle.IsSet();
}

bool UWandererObjectAudioComponent::UpdateAttachment(USceneComponent* InTarget, bool bInEquipped,
    FTransform InRelativeTransform)
{
    UWandererAudioSubsystem* Audio = AudioWeakPtr.Get();
    return Audio && Audio->UpdateObjectEmitter(EmitterHandle,
        MakeAttachment(InTarget, bInEquipped, InRelativeTransform));
}

EObjectAudioRequestResult UWandererObjectAudioComponent::RequestEvent(FName InEventID)
{
    UWandererAudioSubsystem* Audio = AudioWeakPtr.Get();
    return Audio ? Audio->PostObjectEvent(EmitterHandle, InEventID) : EObjectAudioRequestResult::InvalidEmitter;
}

void UWandererObjectAudioComponent::StopEvent(FName InEventID)
{
    if (UWandererAudioSubsystem* Audio = AudioWeakPtr.Get())
    {
        Audio->StopObjectEvent(EmitterHandle, InEventID);
    }
}

void UWandererObjectAudioComponent::DeactivateEmitter()
{
    const FObjectAudioEmitterHandle OldHandle = EmitterHandle;
    EmitterHandle = {};
    if (UWandererAudioSubsystem* Audio = AudioWeakPtr.Get())
    {
        AudioWeakPtr.Reset();
        Audio->UnregisterObjectEmitter(OldHandle);
    }
}

FObjectAudioEmitterStats UWandererObjectAudioComponent::GetEmitterStats() const
{
    const UWandererAudioSubsystem* Audio = AudioWeakPtr.Get();
    return Audio ? Audio->GetObjectEmitterStats(EmitterHandle) : FObjectAudioEmitterStats{};
}

void UWandererObjectAudioComponent::EndPlay(EEndPlayReason::Type InReason)
{
    DeactivateEmitter();
    Super::EndPlay(InReason);
}

FObjectAudioAttachment UWandererObjectAudioComponent::MakeAttachment(USceneComponent* InTarget,
    bool bInEquipped, const FTransform& InRelativeTransform) const
{
    FObjectAudioAttachment Attachment;
    Attachment.ComponentWeakPtr = InTarget;
    Attachment.RelativeTransform = InRelativeTransform;
    if (bInEquipped && Profile)
    {
        Attachment.Socket = Profile->EquipmentSocket;
        Attachment.bFallbackToComponent = Profile->bFallbackToComponent;
    }
    return Attachment;
}
