#pragma once

#include "Features/AudioManager/Authoring/Sources/AudioSourceAuthoringTypes.h"

class UAkRoomComponent;

/** Scoped game-thread commit. Compensates its own mutations without calling global Undo. */
class FAudioSourceCommitOperation
{
public:
    FAudioSourceCommitOperation(
        const FAudioSourceRequest& InRequest, const FAudioSourcePlan& InPlan);
    void Execute(FAudioSourceResult& InOutResult);

private:
    struct FSourceChange
    {
        TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
        FDataTableRowHandle PreviousProfile;
        FDataTableRowHandle WrittenProfile;
        TStrongObjectPtr<UDataTable> PreviousTable;
    };

    struct FRoomChange
    {
        TWeakObjectPtr<UAkRoomComponent> RoomWeakPtr;
        TStrongObjectPtr<UAkAudioEvent> PreviousEvent;
        TWeakObjectPtr<UAkAudioEvent> WrittenEventWeakPtr;
    };

    struct FProfileChange
    {
        FName Name;
        FWandererWorldAudioProfileRow Profile;
    };

    struct FCommittedSource
    {
        TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
        FDataTableRowHandle Handle;
        FWandererWorldAudioProfileRow Profile;
    };

    const FAudioSourceRequest& Request;
    const FAudioSourcePlan& Plan;
    TArray<FSourceChange> SourceChanges;
    TArray<FRoomChange> RoomChanges;
    TArray<FProfileChange> ProfileChanges;
    TArray<TWeakObjectPtr<AActor>> CreatedActorsWeakPtr;
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> CreatedComponentsWeakPtr;
    TArray<FCommittedSource> CommittedSources;
    FString Failure;

    bool CommitItem(const FAudioSourcePlanItem& InItem);
    UWandererAudioSourceComponent* CreateSource(const FAudioSourcePlanItem& InItem);
    UWandererAudioSourceComponent* AttachRoomSource(const FAudioSourcePlanItem& InItem);
    UWandererAudioSourceComponent* SpawnSource(const FAudioSourcePlanItem& InItem);
    bool CommitProfile(const FAudioSourcePlanItem& InItem,
        UWandererAudioSourceComponent& InSource);
    void RememberSource(
        UWandererAudioSourceComponent& InSource, const FDataTableRowHandle& InHandle);
    bool PrepareRoom(const FAudioSourcePlanItem& InItem);
    bool ValidateCommitted() const;
    bool IsSourceLive(const TWeakObjectPtr<UWandererAudioSourceComponent>& InSourceWeakPtr) const;
    void Rollback(FAudioSourceResult& InOutResult);
    void RestoreSources(FAudioSourceResult& InOutResult);
    void RestoreRooms(FAudioSourceResult& InOutResult);
    void RemoveCreatedObjects(FAudioSourceResult& InOutResult);
    void RemoveCreatedProfiles(FAudioSourceResult& InOutResult);


};
