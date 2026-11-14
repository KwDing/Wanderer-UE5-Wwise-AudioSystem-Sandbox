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
#include "Features/AudioManager/Authoring/AudioComponentDeletionOperation.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"

void FAudioManagerController::DeleteRoomSources(bool bInConfirm)
{
    if (!CanEditRoomSources())
    {
        return;
    }
    TArray<TWeakObjectPtr<AWandererWorldAudioEmitter>> Targets;
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> RoomComponents;
    int32 Skipped = 0;
    GatherRoomDeletionTargets(Targets, RoomComponents, Skipped);
    if (Targets.IsEmpty() && RoomComponents.IsEmpty())
    {
        return FinishRoomAction(
            TEXT("没有可删除的独立 Room 底声。挂在其他 Actor 上的组件及自定义子类会保留。"));
    }
    if (bInConfirm && !ConfirmRoomDeletion(Targets.Num() + RoomComponents.Num()))
    {
        return;
    }
    if (!CanEditRoomSources())
    {
        return;
    }
    SetDetailsObjects(TArray<UObject*>());
    if (!CanEditRoomSources())
    {
        return;
    }
    if (GEditor->IsTransactionActive() || GIsTransacting)
    {
        return FinishRoomAction(TEXT("未删除：已有活动事务。"));
    }
    int32 Deleted = 0;
    FString Error;
    if (!RoomComponents.IsEmpty())
    {
        const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
        const TWeakObjectPtr<UWorld> WorldWeakPtr = GetObservedWorld();
        if (!FAudioComponentDeletionOperation::Execute(
                RoomComponents, WorldWeakPtr.Get(), Error,
                [ControllerWeakPtr, WorldWeakPtr]()
                {
                    const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
                    return Controller && Controller->CanEditRoomSources()
                           && Controller->GetObservedWorld() == WorldWeakPtr.Get();
                }))
        {
            return FinishRoomAction(Error);
        }
        Deleted += RoomComponents.Num();
    }
    if (!Targets.IsEmpty())
    {
        // Actor deletion is a separate irreversible phase; retain its Undo on partial failure.
        const FScopedTransaction Transaction(
            FText::FromString(TEXT("Delete standalone Room audio actors")));
        GEditor->GetSelectedActors()->Modify();
        DeleteRoomTargets(Targets, Deleted, Skipped);
        GEditor->NoteSelectionChange();
    }
    FinishRoomAction(FString::Printf(
        TEXT("删除 %d 个；保留/失败 %d 个。组件与独立 Actor 分阶段撤销；部分删除请检查后 Undo。"),
        Deleted, Skipped));
}

void FAudioManagerController::DeleteRoomTargets(
    const TArray<TWeakObjectPtr<AWandererWorldAudioEmitter>>& InTargets, int32& InOutDeleted,
    int32& InOutSkipped)
{
    for (const auto& Target : InTargets)
    {
        AWandererWorldAudioEmitter* Emitter = Target.Get();
        if (!CanEditRoomSources() || !Emitter || Emitter->GetWorld() != GetObservedWorld())
        {
            continue;
        }
        if (FLevelUtils::IsLevelLocked(Emitter->GetLevel()))
        {
            ++InOutSkipped;
            continue;
        }
        const TWeakObjectPtr<UWorld> WorldWeakPtr = GetObservedWorld();
        const TWeakObjectPtr<ULevel> LevelWeakPtr = Emitter->GetLevel();
        Emitter->Modify();
        if (!Target.IsValid() || !CanEditRoomSources() || Target->GetWorld() != WorldWeakPtr.Get()
            || Target->GetLevel() != LevelWeakPtr.Get())
        {
            ++InOutSkipped;
            continue;
        }
        Emitter->GetLevel()->Modify();
        if (!Target.IsValid() || !CanEditRoomSources())
        {
            ++InOutSkipped;
            continue;
        }
        const bool bSelected = Emitter->IsSelected();
        GEditor->SelectActor(Emitter, false, false);
        if (!Target.IsValid() || !CanEditRoomSources() || Target->GetWorld() != WorldWeakPtr.Get()
            || Target->GetLevel() != LevelWeakPtr.Get())
        {
            ++InOutSkipped;
            continue;
        }
        TArray<AActor*> Attached;
        Target->GetAttachedActors(Attached);
        TArray<UWandererAudioSourceComponent*> Sources;
        Target->GetComponents(Sources);
        if (FLevelUtils::IsLevelLocked(Target->GetLevel()) || !Attached.IsEmpty()
            || Target->GetClass() != AWandererWorldAudioEmitter::StaticClass() || Sources.Num() != 1
            || Sources[0] != Target->AudioSource)
        {
            ++InOutSkipped;
            continue;
        }
        if (WorldWeakPtr->EditorDestroyActor(Target.Get(), true))
        {
            ++InOutDeleted;
        }
        else
        {
            if (bSelected && Target.IsValid())
            {
                GEditor->SelectActor(Emitter, true, false);
            }
            ++InOutSkipped;
        }
    }
}

void FAudioManagerController::GatherRoomDeletionTargets(
    TArray<TWeakObjectPtr<AWandererWorldAudioEmitter>>& OutTargetsWeakPtr,
    FRoomSources& OutRoomComponentsWeakPtr, int32& InOutSkipped) const
{
    for (UWandererAudioSourceComponent* Source : GetSelectedRoomSources())
    {
        if (auto* Room = Cast<AAkSpatialAudioVolume>(Source->GetOwner());
            Room && Source->GetClass() == UWandererAudioSourceComponent::StaticClass()
            && Room->GetInstanceComponents().Contains(Source)
            && !FLevelUtils::IsLevelLocked(Room->GetLevel()))
        {
            OutRoomComponentsWeakPtr.AddUnique(Source);
            continue;
        }
        auto* Emitter = Cast<AWandererWorldAudioEmitter>(Source->GetOwner());
        TArray<UWandererAudioSourceComponent*> Components;
        Source->GetOwner()->GetComponents(Components);
        TArray<AActor*> Attached;
        Source->GetOwner()->GetAttachedActors(Attached);
        if (!Emitter || FLevelUtils::IsLevelLocked(Emitter->GetLevel())
            || Emitter->GetClass() != AWandererWorldAudioEmitter::StaticClass()
            || Emitter->AudioSource != Source || Components.Num() != 1 || !Attached.IsEmpty())
        {
            ++InOutSkipped;
            continue;
        }
        OutTargetsWeakPtr.AddUnique(Emitter);
    }
}
