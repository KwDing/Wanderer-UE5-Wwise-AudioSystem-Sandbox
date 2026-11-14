#include "Features/AudioManager/Authoring/Geometry/AudioGeometryCommitOperation.h"

#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

bool FAudioGeometryCommitOperation::Rollback(FAudioSourceResult& InOutResult)
{
    const int32 ErrorCount = InOutResult.Errors.Num();
    for (int32 Index = Changes.Num() - 1; Index >= 0; --Index)
    {
        Restore(Changes[Index], InOutResult);
    }
    RemoveRows(InOutResult);
    InOutResult.Applied = 0;
    const bool bRestored = ErrorCount == InOutResult.Errors.Num();
    InOutResult.Errors.Add(
        bRestored ? TEXT("本批 IF 修改已撤回；此前撤销历史保留。")
                  : TEXT("IF 补偿不完整；保留本批 Undo，请检查残留。"));
    return bRestored;
}

void FAudioGeometryCommitOperation::Restore(FChange& InOutChange, FAudioSourceResult& InOutResult)
{
    const TStrongObjectPtr<AActor> Actor(InOutChange.ActorWeakPtr.Get());
    if (InOutChange.Item.bCreate)
    {
        if (Actor.IsValid() && !Actor->IsActorBeingDestroyed()
            && (!IsValid(Actor->GetWorld())
                || !Actor->GetWorld()->EditorDestroyActor(Actor.Get(), true)))
        {
            InOutResult.Errors.Add(TEXT("撤回新建 IF 声源失败。"));
        }
        return;
    }
    if (!InOutChange.bGeometryWritten && !InOutChange.bMetadataWritten)
    {
        return;
    }
    const TStrongObjectPtr<UWandererAudioSourceComponent> Audio(InOutChange.AudioWeakPtr.Get());
    if (!CanRestore(InOutChange))
    {
        InOutResult.Errors.Add(TEXT("目标已失效，无法恢复其原几何和同步信息。"));
        return;
    }
    FAudioGeometrySnapshot Current;
    if (InOutChange.bGeometryWritten)
    {
        if (!AudioGeometryUtils::Read(Actor.Get(), Current).IsEmpty()
            || !AudioGeometryUtils::MatchesGeometry(Current, InOutChange.Item.Geometry))
        {
            InOutResult.Errors.Add(TEXT("目标几何被外部修改，未覆盖。"));
        }
        else if (!AudioGeometryUtils::Write(Actor.Get(), InOutChange.Item.Previous)
                 || !AudioGeometryUtils::Read(Actor.Get(), Current).IsEmpty()
                 || !AudioGeometryUtils::MatchesGeometry(Current, InOutChange.Item.Previous))
        {
            InOutResult.Errors.Add(TEXT("恢复目标几何失败。"));
        }
    }
    if (!CanRestore(InOutChange))
    {
        InOutResult.Errors.Add(TEXT("恢复几何时目标身份改变，未继续恢复同步信息。"));
        return;
    }
    if (InOutChange.bMetadataWritten && IsValid(Audio.Get()))
    {
        if (AudioGeometryUtils::Matches(AudioGeometryUtils::ReadMetadata(*Audio),
                                        InOutChange.Item.Metadata))
        {
            AudioGeometryUtils::WriteMetadata(*Audio, InOutChange.Item.PreviousMetadata);
            if (!IsValid(Audio.Get())
                || !AudioGeometryUtils::Matches(AudioGeometryUtils::ReadMetadata(*Audio),
                                                InOutChange.Item.PreviousMetadata))
            {
                InOutResult.Errors.Add(TEXT("恢复通知再次改变了同步信息。"));
            }
        }
        else
        {
            InOutResult.Errors.Add(TEXT("同步元数据被外部修改，未覆盖。"));
        }
    }
}

bool FAudioGeometryCommitOperation::CanRestore(const FChange& InChange) const
{
    AActor* Actor = InChange.ActorWeakPtr.Get();
    UWandererAudioSourceComponent* Audio = InChange.AudioWeakPtr.Get();
    return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && IsValid(Audio)
           && Audio->GetOwner() == Actor && InChange.RootWeakPtr.IsValid()
           && Actor->GetRootComponent() == InChange.RootWeakPtr.Get()
           && Actor->GetWorld() == Plan.WorldWeakPtr.Get()
           && Actor->GetLevel() == InChange.LevelWeakPtr.Get();
}

void FAudioGeometryCommitOperation::RemoveRows(FAudioSourceResult& InOutResult)
{
    for (int32 Index = CreatedRows.Num() - 1; Index >= 0; --Index)
    {
        const FCreatedRow& Row = CreatedRows[Index];
        if (Row.Name.IsNone())
        {
            continue;
        }
        bool bReferenced = false;
        for (TObjectIterator<UWandererAudioSourceComponent> It; It; ++It)
        {
            if (IsValid(*It) && IsValid(It->GetOwner()) && !It->GetOwner()->IsActorBeingDestroyed())
            {
                bReferenced
                    |= It->Profile.DataTable == Plan.Table.Get() && It->Profile.RowName == Row.Name;
            }
        }
        if (bReferenced || !Plan.Table.IsValid()
            || !FWorldAudioProfileRepository::RemoveCreated(*Plan.Table, Row.Name, Row.Profile))
        {
            InOutResult.Errors.Add(TEXT("新增配置行被引用或被外部修改，已保留：")
                                   + Row.Name.ToString());
        }
    }
    // Dirty flags stay conservative: callbacks may have made unrelated edits.
}
