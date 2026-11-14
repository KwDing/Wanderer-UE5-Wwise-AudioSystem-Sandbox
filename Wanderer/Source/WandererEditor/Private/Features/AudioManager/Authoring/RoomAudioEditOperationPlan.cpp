#include "Features/AudioManager/Authoring/RoomAudioEditOperation.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "LevelUtils.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

namespace
{
    bool FindGroup(AAkSpatialAudioVolume* InRoom, FName& OutGroup)
    {
        int32 Count = 0;
        for (TActorIterator<AWandererWorldAudioBudget> It(InRoom->GetWorld()); It; ++It)
        {
            for (const FWandererAudioResourceGroup& Group : It->ResourceGroups)
            {
                if (Group.Rooms.ContainsByPredicate([InRoom](const TSoftObjectPtr<AActor>& InMember)
                                                    { return InMember.Get() == InRoom; }))
                {
                    OutGroup = Group.Name;
                    ++Count;
                }
            }
            break;
        }
        return Count <= 1;
    }

    AAkSpatialAudioVolume* FindUniqueRoom(UWandererAudioSourceComponent& InSource)
    {
        AAkSpatialAudioVolume* Found = nullptr;
        for (TActorIterator<AAkSpatialAudioVolume> It(InSource.GetWorld()); It; ++It)
        {
            if (!IsValid(It->Room) || !It->Room->bEnable || It->Room->AutoPost
                || !It->EncompassesPoint(InSource.GetOwner()->GetActorLocation()))
            {
                continue;
            }
            if (Found)
            {
                return nullptr;
            }
            Found = *It;
        }
        return Found;
    }
} // namespace

bool FRoomAudioEditOperation::CanContinue() const
{
    if (Request.CanContinue && !Request.CanContinue())
    {
        return false;
    }
    return IsInGameThread() && GEditor && !GEditor->PlayWorld && Request.WorldWeakPtr.IsValid()
           && !Request.WorldWeakPtr->bIsTearingDown
           && Request.WorldWeakPtr.Get() == GEditor->GetEditorWorldContext().World();
}

bool FRoomAudioEditOperation::Build(FAudioSourceResult& InOutResult)
{
    if (!CanContinue())
    {
        return false;
    }
    const bool bEvents = Request.Action == ERoomAudioEdit::Events
                         || Request.Action == ERoomAudioEdit::CreateOrApply;
    if (bEvents)
    {
        Event.Reset(Cast<UAkAudioEvent>(Request.EventSoftRef.TryLoad()));
        NightEvent.Reset(Cast<UAkAudioEvent>(Request.NightEventSoftRef.TryLoad()));
        FallbackTable.Reset(Cast<UDataTable>(Request.TableSoftRef.TryLoad()));
        if (!Event.IsValid() || (!Request.NightEventSoftRef.IsNull() && !NightEvent.IsValid()))
        {
            return false;
        }
    }
    if (!CanContinue())
    {
        return false;
    }
    TSet<TWeakObjectPtr<AAkSpatialAudioVolume>> Occupied;
    for (TActorIterator<AActor> It(Request.WorldWeakPtr.Get()); It; ++It)
    {
        TArray<UWandererAudioSourceComponent*> Sources;
        It->GetComponents(Sources);
        for (UWandererAudioSourceComponent* Source : Sources)
        {
            if (AAkSpatialAudioVolume* Room = Source->ResolveRoom())
            {
                Occupied.Add(Room);
            }
        }
    }
    if (Request.Action == ERoomAudioEdit::Create || Request.Action == ERoomAudioEdit::CreateOrApply)
    {
        for (const TWeakObjectPtr<AActor>& RoomWeakPtr : Request.RoomsWeakPtr)
        {
            AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(RoomWeakPtr.Get());
            if (!Room || Occupied.Contains(Room) || !AddItem(Room, nullptr))
            {
                ++InOutResult.Skipped;
                continue;
            }
            Occupied.Add(Room);
        }
    }
    if (Request.Action != ERoomAudioEdit::Create)
    {
        TSet<TWeakObjectPtr<UWandererAudioSourceComponent>> Seen;
        for (const TWeakObjectPtr<UWandererAudioSourceComponent>& SourceWeakPtr :
             Request.SourcesWeakPtr)
        {
            UWandererAudioSourceComponent* Source = SourceWeakPtr.Get();
            if (!IsValid(Source) || !IsValid(Source->GetOwner()) || Seen.Contains(SourceWeakPtr)
                || Source->IsA<UWandererBuildingAmbienceComponent>())
            {
                ++InOutResult.Skipped;
                continue;
            }
            Seen.Add(SourceWeakPtr);
            AAkSpatialAudioVolume* Room = Source->ResolveRoom();
            if (Request.Action == ERoomAudioEdit::Associate)
            {
                Room = Source->AcousticRoom.IsNull() ? FindUniqueRoom(*Source) : nullptr;
            }
            if (!Room || !AddItem(Room, Source))
            {
                ++InOutResult.Skipped;
                InOutResult.Errors.Add(
                    Room && Room->Room && Room->Room->AutoPost
                        ? TEXT("Room Auto Post 开启，请关闭以防重复播放。")
                        : TEXT("跳过：Room/Profile/资源组无效、关卡锁定或无法唯一关联。"));
            }
        }
    }
    return CanContinue() && !Items.IsEmpty();
}

bool FRoomAudioEditOperation::AddItem(AAkSpatialAudioVolume* InRoom,
                                      UWandererAudioSourceComponent* InSource)
{
    FItem Item;
    Item.RoomWeakPtr = InRoom;
    Item.RoomComponentWeakPtr = InRoom->Room.Get();
    Item.SourceWeakPtr = InSource;
    Item.bCreate = InSource == nullptr;
    AActor* Owner = InSource ? InSource->GetOwner() : InRoom;
    if (!IsValid(Owner))
    {
        return false;
    }
    Item.OwnerWeakPtr = Owner;
    Item.LevelWeakPtr = Owner->GetLevel();
    if (!FindGroup(InRoom, Item.Group) || !IsLive(Item))
    {
        return false;
    }
    Item.PreviousRoomEvent.Reset(InRoom->Room->AkAudioEvent);
    Item.WrittenProfile = Request.Profile;
    if (InSource)
    {
        Item.PreviousProfile = InSource->Profile;
        Item.PreviousTable.Reset(const_cast<UDataTable*>(InSource->Profile.DataTable.Get()));
        Item.PreviousRoomSoftPtr = InSource->AcousticRoom;
        Item.PreviousGroup = InSource->ResourceGroup;
        if (Request.Action == ERoomAudioEdit::Associate && !Item.PreviousGroup.IsNone()
            && Item.PreviousGroup != Item.Group)
        {
            return false;
        }
    }
    if (Request.Action == ERoomAudioEdit::Associate)
    {
        Item.WrittenProfile = InSource->Profile;
    }
    const FWandererWorldAudioProfileRow* Row
        = Item.WrittenProfile.DataTable
              ? Item.WrittenProfile.GetRow<FWandererWorldAudioProfileRow>(TEXT("Room edit plan"))
              : nullptr;
    if (Row)
    {
        Item.Row = *Row;
    }
    Item.Table.Reset(const_cast<UDataTable*>(Item.WrittenProfile.DataTable.Get()));
    if (Event.IsValid())
    {
        const FWandererWorldAudioProfileRow* Existing = InSource ? InSource->GetProfile() : Row;
        Item.Table.Reset(Existing && Existing->Mode == EWandererWorldAudioMode::Room
                             ? const_cast<UDataTable*>(
                                   (InSource ? InSource->Profile : Request.Profile).DataTable.Get())
                             : FallbackTable.Get());
        Item.Row = Existing && Existing->Mode == EWandererWorldAudioMode::Room
                       ? *Existing
                       : FWandererWorldAudioProfileRow();
        Item.Row.Mode = EWandererWorldAudioMode::Room;
        Item.Row.PlayEvent = Event.Get();
        Item.Row.NightPlayEvent = NightEvent.Get();
    }
    if (!Item.Table.IsValid()
        || Item.Table->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct()
        || Item.Row.Mode != EWandererWorldAudioMode::Room || !Item.Row.Validate().IsEmpty())
    {
        return false;
    }
    Item.Event.Reset(Item.Row.PlayEvent.LoadSynchronous());
    if (!CanContinue() || !IsLive(Item) || !Item.Event.IsValid())
    {
        return false;
    }
    Items.Add(MoveTemp(Item));
    return true;
}

bool FRoomAudioEditOperation::IsLive(const FItem& InItem) const
{
    AActor* Owner = InItem.OwnerWeakPtr.Get();
    AAkSpatialAudioVolume* Room = InItem.RoomWeakPtr.Get();
    UAkRoomComponent* Component = InItem.RoomComponentWeakPtr.Get();
    if (!IsValid(Owner) || Owner->IsActorBeingDestroyed()
        || Owner->GetWorld() != Request.WorldWeakPtr.Get()
        || Owner->GetLevel() != InItem.LevelWeakPtr.Get()
        || FLevelUtils::IsLevelLocked(Owner->GetLevel()) || !IsValid(Room) || !IsValid(Component)
        || Room->Room != Component || Room->GetWorld() != Request.WorldWeakPtr.Get()
        || FLevelUtils::IsLevelLocked(Room->GetLevel()) || !Component->bEnable
        || Component->AutoPost)
    {
        return false;
    }
    return (InItem.bCreate && !InItem.bCreated)
           || (InItem.SourceWeakPtr.IsValid() && InItem.SourceWeakPtr->GetOwner() == Owner);
}

bool FRoomAudioEditOperation::MatchesRoomGroup(const FItem& InItem) const
{
    if (!InItem.bCreate && Request.Action != ERoomAudioEdit::Associate)
    {
        return true;
    }
    AAkSpatialAudioVolume* Room = InItem.RoomWeakPtr.Get();
    FName CurrentGroup;
    return Room && FindGroup(Room, CurrentGroup) && CurrentGroup == InItem.Group;
}
