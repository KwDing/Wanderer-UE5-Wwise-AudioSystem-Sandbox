#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Core/ActorPathUtils.h"
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
    const bool bReady = bBuildingEnabled && Error.IsEmpty() && bGroupReady && bInside;
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
    if (!bReady && GetPlayingID() == 0 && BuildingEmitter)
    {
        BuildingEmitter->DestroyComponent();
        BuildingEmitter = nullptr;
    }
    if (BuildingEmitter)
    {
        BuildingEmitter->SetWorldLocation(ListenerPosition);
    }
    return bReady;
}

UAkGameObject* UWandererBuildingAmbienceComponent::GetPlaybackObject()
{
    if (!BuildingEmitter && HasBegunPlay() && IsValid(GetOwner()))
    {
        BuildingEmitter = NewObject<UAkComponent>(GetOwner(), NAME_None, RF_Transient);
        BuildingEmitter->bUseReverbVolumes = false;
        BuildingEmitter->OcclusionRefreshInterval = 0;
        BuildingEmitter->StopWhenOwnerDestroyed = true;
        BuildingEmitter->SetWorldLocation(ListenerPosition);
        BuildingEmitter->RegisterComponent();
    }
    return BuildingEmitter;
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

void UWandererBuildingAmbienceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason); // Stops owned Playing ID and returns leases before emitter destruction.
    if (BuildingEmitter)
    {
        BuildingEmitter->DestroyComponent();
    }
    BuildingEmitter = nullptr;
}

void UWandererBuildingAmbienceComponent::PreparePlaybackObject(UAkGameObject* Object)
{
    // Per-game-object RTPCs preserve ordinary Room instances of the same Event.
    Object->SetRTPCValue(nullptr, 1, 0, TEXT("BuildingAmbience_Mode"));
    Object->SetRTPCValue(nullptr, 0, 0, TEXT("BuildingAmbience_Gain"));
}

void UWandererBuildingAmbienceComponent::PlaybackStarted(UAkGameObject* Object)
{
    Object->SetRTPCValue(
        nullptr, 1, FMath::RoundToInt(FadeInSeconds * 1000), TEXT("BuildingAmbience_Gain"));
}
