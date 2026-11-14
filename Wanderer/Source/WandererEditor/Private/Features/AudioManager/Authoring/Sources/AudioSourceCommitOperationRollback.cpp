#include "Features/AudioManager/Authoring/Sources/AudioSourceCommitOperation.h"

#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

void FAudioSourceCommitOperation::Rollback(FAudioSourceResult& InOutResult)
{
    const int32 PreviousErrors = InOutResult.Errors.Num();
    RestoreSources(InOutResult);
    RestoreRooms(InOutResult);
    RemoveCreatedObjects(InOutResult);
    RemoveCreatedProfiles(InOutResult);
    InOutResult.Errors.Add(InOutResult.Errors.Num() == PreviousErrors
        ? TEXT("本批修改已撤回；此前撤销历史保留。")
        : TEXT("补偿未完全完成：外部回调发生冲突，已保留相关内容，请检查上述残留。"));
}

void FAudioSourceCommitOperation::RestoreSources(FAudioSourceResult& InOutResult)
{
    for (int32 Index = SourceChanges.Num() - 1; Index >= 0; --Index)
    {
        const FSourceChange& Change = SourceChanges[Index];
        UWandererAudioSourceComponent* Source = Change.SourceWeakPtr.Get();
        if (!IsValid(Source))
        {
            InOutResult.Errors.Add(TEXT("回调销毁了声源，无法恢复该对象；请检查场景。"));
            continue;
        }
        if (Source->Profile != Change.WrittenProfile && Source->Profile != Change.PreviousProfile)
        {
            InOutResult.Errors.Add(TEXT("声源引用被外部修改，未覆盖外部配置。"));
            continue;
        }
        Source->Profile = Change.PreviousProfile;
        Source->PostEditChange();
        if (!Change.SourceWeakPtr.IsValid()
            || Change.SourceWeakPtr->Profile != Change.PreviousProfile)
        {
            InOutResult.Errors.Add(TEXT("恢复通知再次改变了声源引用，请检查场景。"));
        }
    }
}

void FAudioSourceCommitOperation::RestoreRooms(FAudioSourceResult& InOutResult)
{
    for (int32 Index = RoomChanges.Num() - 1; Index >= 0; --Index)
    {
        const FRoomChange& Change = RoomChanges[Index];
        UAkRoomComponent* Room = Change.RoomWeakPtr.Get();
        if (!IsValid(Room))
        {
            InOutResult.Errors.Add(TEXT("回调销毁了Room组件，无法恢复其原Event。"));
            continue;
        }
        if (Room->AkAudioEvent != Change.WrittenEventWeakPtr.Get()
            && Room->AkAudioEvent != Change.PreviousEvent.Get())
        {
            InOutResult.Errors.Add(TEXT("Room Event被外部修改，未覆盖外部配置。"));
            continue;
        }
        Room->AkAudioEvent = Change.PreviousEvent.Get();
        Room->PostEditChange();
        if (!Change.RoomWeakPtr.IsValid()
            || Change.RoomWeakPtr->AkAudioEvent != Change.PreviousEvent.Get())
        {
            InOutResult.Errors.Add(TEXT("恢复通知再次改变了Room Event，请检查场景。"));
        }
    }
}

void FAudioSourceCommitOperation::RemoveCreatedObjects(FAudioSourceResult& InOutResult)
{
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& SourceWeakPtr
        : CreatedComponentsWeakPtr)
    {
        UWandererAudioSourceComponent* Source = SourceWeakPtr.Get();
        if (!IsValid(Source))
        {
            continue;
        }
        if (AActor* Owner = Source->GetOwner())
        {
            Owner->RemoveInstanceComponent(Source);
        }
        Source->DestroyComponent();
    }
    for (const TWeakObjectPtr<AActor>& ActorWeakPtr : CreatedActorsWeakPtr)
    {
        AActor* Actor = ActorWeakPtr.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
        {
            continue;
        }
        UWorld* World = Actor->GetWorld();
        if (!IsValid(World) || !World->DestroyActor(Actor))
        {
            InOutResult.Errors.Add(TEXT("撤回新建声源Actor失败，请检查场景。"));
        }
    }
}

void FAudioSourceCommitOperation::RemoveCreatedProfiles(FAudioSourceResult& InOutResult)
{
    if (!Plan.Table.IsValid())
    {
        InOutResult.Errors.Add(TEXT("配置表已失效，无法检查新增行的撤回结果。"));
        return;
    }
    for (int32 Index = ProfileChanges.Num() - 1; Index >= 0; --Index)
    {
        const FProfileChange& Change = ProfileChanges[Index];
        bool bReferenced = false;
        for (TObjectIterator<UWandererAudioSourceComponent> It; It; ++It)
        {
            if (!IsValid(*It) || !IsValid(It->GetOwner())
                || It->GetOwner()->IsActorBeingDestroyed())
            {
                continue;
            }
            bReferenced |= It->Profile.DataTable == Plan.Table.Get()
                && It->Profile.RowName == Change.Name;
        }
        if (bReferenced)
        {
            InOutResult.Errors.Add(TEXT("新增行仍被声源引用，保留避免悬空：") + Change.Name.ToString());
            continue;
        }
        if (!FWorldAudioProfileRepository::RemoveCreated(*Plan.Table, Change.Name, Change.Profile))
        {
            InOutResult.Errors.Add(TEXT("配置行被外部回调改写，保留以避免覆盖外部修改：")
                + Change.Name.ToString());
        }
    }
    // Keep packages dirty: callbacks may have made unrelated changes that must still be saved.
}
