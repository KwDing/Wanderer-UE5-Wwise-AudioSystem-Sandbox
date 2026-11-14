#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

#include "Features/AudioManager/Authoring/Sources/AudioSourceAuthoringTypes.h"
#include "Features/AudioManager/Authoring/RoomAudioTypes.h"

class UAkRoomComponent;

/** Room overrides own source profiles and missing Wwise metadata, never shared building records. */
class FRoomAudioEditOperation
{
  public:
    /** Owns one game-thread transaction; nested callers are rejected. No assets are saved. */
    static FAudioSourceResult Execute(const FRoomAudioEditRequest& InRequest);

  private:
    struct FItem
    {
        TWeakObjectPtr<AActor> OwnerWeakPtr;
        TWeakObjectPtr<ULevel> LevelWeakPtr;
        TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
        TWeakObjectPtr<AAkSpatialAudioVolume> RoomWeakPtr;
        TWeakObjectPtr<UAkRoomComponent> RoomComponentWeakPtr;
        TStrongObjectPtr<UDataTable> Table;
        TStrongObjectPtr<UDataTable> PreviousTable;
        TStrongObjectPtr<UAkAudioEvent> Event;
        TStrongObjectPtr<UAkAudioEvent> PreviousRoomEvent;
        FWandererWorldAudioProfileRow Row;
        FDataTableRowHandle PreviousProfile;
        FDataTableRowHandle WrittenProfile;
        TSoftObjectPtr<AActor> PreviousRoomSoftPtr;
        FName PreviousGroup;
        FName Group;
        FName CreatedRow;
        bool bCreate = false;
        bool bCreated = false;
        bool bWritten = false;
        bool bRoomWritten = false;
    };

    explicit FRoomAudioEditOperation(const FRoomAudioEditRequest& InRequest);
    FRoomAudioEditRequest Request;
    TArray<FItem> Items;
    TStrongObjectPtr<UAkAudioEvent> Event;
    TStrongObjectPtr<UAkAudioEvent> NightEvent;
    TStrongObjectPtr<UDataTable> FallbackTable;
    TStrongObjectPtr<UDataTable> ProfileTable;

    bool CanContinue() const;
    bool Build(FAudioSourceResult& InOutResult);
    bool AddItem(AAkSpatialAudioVolume* InRoom, UWandererAudioSourceComponent* InSource);
    bool IsLive(const FItem& InItem) const;
    bool MatchesRoomGroup(const FItem& InItem) const;
    bool IsUnchanged(const FItem& InItem) const;
    bool Matches(const FItem& InItem) const;
    bool Commit(FItem& InOutItem);
    bool PrepareRoom(FItem& InOutItem);
    bool Rollback();
};
