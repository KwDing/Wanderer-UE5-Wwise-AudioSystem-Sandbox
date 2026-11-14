#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"

bool FAudioManagerController::CanEditRoomSources() const
{
    if (bStopped)
    {
        return false;
    }
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (Entry->ActorWeakPtr.IsValid()
            && Entry->ActorWeakPtr->ActorHasTag(TEXT("AudioAutoFitPreview")))
        {
            return false;
        }
    }
    return GEditor && !GEditor->PlayWorld && GetObservedWorld()
           && GetObservedWorld()->WorldType == EWorldType::Editor
           && (State.ActivePage == 0 || State.ActivePage == 3) && !State.bProjectAssets;
}

TArray<AAkSpatialAudioVolume*> FAudioManagerController::GetSelectedRooms() const
{
    TArray<AAkSpatialAudioVolume*> Rooms;
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        auto* Room = Cast<AAkSpatialAudioVolume>(Entry->ActorWeakPtr.Get());
        if (!Room && Entry->SourceWeakPtr.IsValid())
        {
            Room = Entry->SourceWeakPtr->ResolveRoom();
        }
        if (IsValid(Room) && Room->GetWorld() == GetObservedWorld())
        {
            Rooms.AddUnique(Room);
        }
    }
    return Rooms;
}

TArray<UWandererAudioSourceComponent*> FAudioManagerController::GetSelectedRoomSources() const
{
    TArray<UWandererAudioSourceComponent*> Sources;
    TArray<AAkSpatialAudioVolume*> Rooms;
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (auto* Room = Cast<AAkSpatialAudioVolume>(Entry->ActorWeakPtr.Get());
            Room && Room->GetWorld() == GetObservedWorld())
        {
            Rooms.AddUnique(Room);
        }
    }
    UWorld* World = GetObservedWorld();
    if (!World)
    {
        return Sources;
    }
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TArray<UWandererAudioSourceComponent*> Components;
        It->GetComponents(Components);
        for (UWandererAudioSourceComponent* Source : Components)
        {
            if (Source->ResolveRoom() && Rooms.Contains(Source->ResolveRoom()))
            {
                Sources.AddUnique(Source);
            }
        }
    }
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (UWandererAudioSourceComponent* Source = Entry->SourceWeakPtr.Get();
            Source && Source->GetWorld() == World)
        {
            const FWandererWorldAudioProfileRow* Profile = Source->GetProfile();
            if (Source->ResolveRoom()
                || (Profile && Profile->Mode == EWandererWorldAudioMode::Room))
            {
                Sources.AddUnique(Source);
            }
        }
    }
    return Sources;
}

void FAudioManagerController::RefreshRoomProfiles()
{
    if (bStopped)
    {
        return;
    }
    State.RoomProfileOptions.Reset();
    auto* Table = Cast<UDataTable>(State.RoomProfileTablePath.TryLoad());
    if (Table && Table->GetRowStruct() == FWandererWorldAudioProfileRow::StaticStruct())
    {
        for (const FName Name : Table->GetRowNames())
        {
            const FWandererWorldAudioProfileRow* Row =
                Table->FindRow<FWandererWorldAudioProfileRow>(Name, TEXT("Room manager"));
            if (Row && Row->Mode == EWandererWorldAudioMode::Room && Row->Validate().IsEmpty())
            {
                State.RoomProfileOptions.Add(MakeShared<FString>(Name.ToString()));
            }
        }
    }
    State.RoomProfileOptions.Sort(
        [](const auto& A, const auto& B)
        {
            return *A < *B;
        });
    if (!State.RoomProfileOptions.ContainsByPredicate(
            [this](const auto& Item)
            {
                return FName(**Item) == State.RoomProfileRow;
            }))
    {
        State.RoomProfileRow = NAME_None;
    }
    RefreshRoomOptions();
}

bool FAudioManagerController::GetRoomProfile(FDataTableRowHandle& OutProfile) const
{
    auto* Table = Cast<UDataTable>(State.RoomProfileTablePath.TryLoad());
    if (!Table || Table->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return false;
    }
    const FWandererWorldAudioProfileRow* Row = Table->FindRow<FWandererWorldAudioProfileRow>(
        State.RoomProfileRow, TEXT("Room manager"), false);
    if (!Row || Row->Mode != EWandererWorldAudioMode::Room || !Row->Validate().IsEmpty())
    {
        return false;
    }
    OutProfile.DataTable = Table;
    OutProfile.RowName = State.RoomProfileRow;
    return true;
}

void FAudioManagerController::RefreshSelectedRoomEvent()
{
    if (bStopped)
    {
        return;
    }
    const auto Sources = GetSelectedRoomSources();
    FSoftObjectPath CommonEvent;
    FSoftObjectPath CommonNight;
    bool bFirst = true;
    bool bMixed = false;
    bool bMixedNight = false;
    for (const UWandererAudioSourceComponent* Source : Sources)
    {
        const FWandererWorldAudioProfileRow* Profile = Source->GetProfile();
        const FSoftObjectPath Event =
            Profile ? Profile->PlayEvent.ToSoftObjectPath() : FSoftObjectPath();
        const FSoftObjectPath Night =
            Profile ? Profile->NightPlayEvent.ToSoftObjectPath() : FSoftObjectPath();
        bMixed |= !bFirst && CommonEvent != Event;
        bMixedNight |= !bFirst && CommonNight != Night;
        CommonEvent = Event;
        CommonNight = Night;
        bFirst = false;
    }
    if (bMixed)
    {
        CommonEvent.Reset();
    }
    if (bMixedNight)
    {
        CommonNight.Reset();
    }
    TArray<TWeakObjectPtr<AActor>> Targets;
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (Entry->ActorWeakPtr.IsValid())
        {
            Targets.AddUnique(Entry->ActorWeakPtr);
        }
    }
    const bool bSelectionChanged = Targets.Num() != RoomEventTargetsWeakPtr.Num()
                                   || Targets.ContainsByPredicate(
                                       [this](const auto& Target)
                                       {
                                           return !RoomEventTargetsWeakPtr.Contains(Target);
                                       });
    if (bSelectionChanged || CommonEvent != ObservedRoomPlayEvent
        || CommonNight != ObservedRoomNightEvent)
    {
        State.RoomEventPath = CommonEvent;
        State.RoomNightEventPath = CommonNight;
    }
    RoomEventTargetsWeakPtr = MoveTemp(Targets);
    ObservedRoomPlayEvent = CommonEvent;
    ObservedRoomNightEvent = CommonNight;
    State.SelectedRoomEvent = FText::FromString(
        Sources.IsEmpty() ? TEXT("未选择关联底声")
        : bMixed          ? TEXT("当前 Play Event：多个不同 Event")
                          : TEXT("当前 Play Event：")
                       + (CommonEvent.IsNull() ? TEXT("未配置") : CommonEvent.GetAssetName()));
}
