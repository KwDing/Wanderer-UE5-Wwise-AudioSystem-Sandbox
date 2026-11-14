#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"

class FStructOnScope;
class UWandererAudioSourceComponent;

struct FAudioManagerColumn
{
    FName Key;
    FString Label;
    float Width;
    bool bDefaultVisible;
};

struct FAudioManagerEntry
{
    TWeakObjectPtr<AActor> ActorWeakPtr;
    TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
    FString Description;
    FString Kind;
    FString Group;
    bool bPrimaryRoomRow = false;
    FName ResourceGroupName;
    FSoftObjectPath AssetPath;
    TMap<FName, FString> Cells;
};

using FAudioManagerEntryPtr = TSharedPtr<FAudioManagerEntry>;

struct FAudioManagerState
{
    bool bShowRoomActions = false;
    bool bDetailsHasBuildingSource = false;
    FString SharedRoomNotice;
    TArray<FAudioManagerEntryPtr> Entries;
    TSet<FName> HiddenSourceColumns;
    TSet<FName> HiddenResourceColumns;
    FText SelectedBuildingResourceGroup;
    TArray<TSharedPtr<FString>> GroupOptions;
    FString TypeFilter = TEXT("全部类型");
    FString GroupFilter = TEXT("全部资源组");
    FName SortColumn = TEXT("Name");
    EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;
    int32 ActivePage = 0;
    bool bProjectAssets = false;
    FString Filter;
    FSoftObjectPath RoomProfileTablePath = FSoftObjectPath(
        TEXT("/Game/Config/Audio/WorldAudio/DT_WorldAudioProfiles.DT_WorldAudioProfiles"));
    FName RoomProfileRow = TEXT("AMB_ROOM");
    TArray<TSharedPtr<FString>> RoomProfileOptions;
    FString LastRoomAction;
    FString ResourceConfigurationNotice;
    FSoftObjectPath RoomEventPath;
    FSoftObjectPath RoomNightEventPath;
    FText SelectedRoomEvent;
    FText Summary;
};

struct FAudioSourceDialogDraft
{
    FAudioSourceRequest Request;
    TSharedPtr<FStructOnScope> Data;
    FText Result;
    FString ProfileRowName;
    bool bCustomProfileRowName = false;
};

struct FAudioBuildingDraft
{
    FString Name;
    FSoftObjectPath Event;
    TArray<TWeakObjectPtr<AActor>> RoomsWeakPtr;
    TWeakObjectPtr<UWorld> WorldWeakPtr;
    FText Error;
};
