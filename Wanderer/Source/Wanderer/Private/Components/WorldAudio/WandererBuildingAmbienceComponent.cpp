#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Core/ActorPathUtils.h"
#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "EngineUtils.h"
#include "Engine/World.h"

const FWandererWorldAudioProfileRow* UWandererBuildingAmbienceComponent::GetProfile() const
{
    PlaybackProfile.Mode = EWandererWorldAudioMode::Global;
    PlaybackProfile.PlayEvent = BuildingEvent;
    PlaybackProfile.NightPlayEvent = BuildingNightEvent;
    PlaybackProfile.Priority = BuildingPriority;
    PlaybackProfile.FadeOutSec = BuildingFadeOutSeconds;
    PlaybackProfile.FadeInSec = 0; // BuildingGain already provides the authored fade-in.
    PlaybackProfile.ReverbTailHoldSec = ReverbTailHoldSec;
    return &PlaybackProfile;
}

FString UWandererBuildingAmbienceComponent::ValidateConfiguration() const
{
    if (BuildingName.IsNone())
    {
        return TEXT("缺少建筑名称");
    }
    if (MemberRooms.IsEmpty())
    {
        return TEXT("没有成员 Room；请添加成员或删除这条建筑底噪");
    }
    if (!FMath::IsFinite(FadeInSeconds) || FadeInSeconds < 0 || FadeInSeconds > 2)
    {
        return TEXT("淡入须为 0～2 秒");
    }
    if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
    {
        const FString MembershipError = ValidateRoomMembership();
        if (!MembershipError.IsEmpty())
        {
            return MembershipError;
        }
        const FString OwnershipError = ValidateBuildingOwnership();
        if (!OwnershipError.IsEmpty())
        {
            return OwnershipError;
        }
    }

    return GetProfile()->Validate();
}

bool UWandererBuildingAmbienceComponent::Evaluate(const FVector& Listener)
{
    ListenerPosition = Listener;
    const FString Error = ValidateConfiguration();
    const UWandererAudioSubsystem* System =
        GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
    const bool bGroupReady = System && System->IsResourceGroupActive(ResourceGroup);
    const bool bInside = System && System->IsListenerInsideBuilding(this);
    const bool bAvailable = bBuildingEnabled && Error.IsEmpty() && bGroupReady;
    const bool bReady = bAvailable && bInside;
    FString State = bReady ? TEXT("Inside building") : TEXT("Outside building / disabled");
    if (!bGroupReady)
    {
        State = System ? System->GetResourceGroupStatus(ResourceGroup)
                       : TEXT("Audio subsystem unavailable");
    }
    if (!Error.IsEmpty())
    {
        State = Error;
    }
    SetEvaluationState(BuildingPriority, false, State);
    if (UAkComponent* Emitter = GetOwnedEmitter())
    {
        Emitter->SetWorldLocation(ListenerPosition);
    }
    return bReady;
}

const UObject* UWandererBuildingAmbienceComponent::GetPlaybackIdentity() const
{
    return this;
}

UAkGameObject* UWandererBuildingAmbienceComponent::GetPlaybackObject() const
{
    return GetOwnedEmitter();
}

UAkGameObject* UWandererBuildingAmbienceComponent::AcquirePlaybackObject()
{
    if (UAkComponent* Emitter = AcquireWorldEmitter(true))
    {
        Emitter->bUseReverbVolumes = false;
        Emitter->SetWorldLocation(ListenerPosition);
    }
    return GetPlaybackObject();
}

bool UWandererBuildingAmbienceComponent::ContainsRoom(const AActor* Room) const
{
    if (!Room || Room->GetWorld() != GetWorld())
    {
        return false;
    }
    return MemberRooms.ContainsByPredicate(
        [Room](const auto& Member)
        {
            return ActorPathUtils::ForWorld(Member.ToSoftObjectPath(), Room->GetWorld())
                   == FSoftObjectPath(Room);
        });
}

void UWandererBuildingAmbienceComponent::PreparePlaybackObject(UAkGameObject* Object)
{
    // Per-game-object RTPCs preserve ordinary Room instances of the same Event.
    WandererAudioPlayback::SetWorldEmitterParameter(*Object, EWorldAudioEmitterParameter::BuildingMode, 1, 0);
    WandererAudioPlayback::SetWorldEmitterParameter(*Object, EWorldAudioEmitterParameter::BuildingGain, 0, 0);
}

void UWandererBuildingAmbienceComponent::PlaybackStarted(UAkGameObject* Object)
{
    WandererAudioPlayback::SetWorldEmitterParameter(*Object, EWorldAudioEmitterParameter::BuildingGain,
        1, FMath::RoundToInt(FadeInSeconds * 1000));
}
