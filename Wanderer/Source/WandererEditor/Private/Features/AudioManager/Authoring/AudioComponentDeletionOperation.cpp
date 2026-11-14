#include "Features/AudioManager/Authoring/AudioComponentDeletionOperation.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Level.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "UObject/GCObject.h"

#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

namespace
{
    struct FDeletedComponent : FGCObject
    {
        FTransaction Snapshot;
        TObjectPtr<UWandererAudioSourceComponent> Component = nullptr;
        TWeakObjectPtr<AActor> OwnerWeakPtr;
        TWeakObjectPtr<ULevel> LevelWeakPtr;
        bool bRegistered = false;
        bool bDeleted = false;

        virtual void AddReferencedObjects(FReferenceCollector& InCollector) override
        {
            InCollector.AddReferencedObject(Component);
            Snapshot.AddReferencedObjects(InCollector);
        }

        virtual FString GetReferencerName() const override
        {
            return TEXT("AudioComponentDeletion");
        }

        bool IsLive(UWorld* InWorld) const
        {
            AActor* Owner = OwnerWeakPtr.Get();
            return IsValid(Component) && IsValid(Owner) && !Owner->IsActorBeingDestroyed()
                   && Owner->GetWorld() == InWorld && Owner->GetLevel() == LevelWeakPtr.Get()
                   && Component->GetOwner() == Owner
                   && Owner->GetInstanceComponents().Contains(Component)
                   && !FLevelUtils::IsLevelLocked(Owner->GetLevel());
        }

        bool Restore(UWorld* InWorld)
        {
            if (!bDeleted)
            {
                return true;
            }
            AActor* Owner = OwnerWeakPtr.Get();
            if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->GetWorld() != InWorld
                || Owner->GetLevel() != LevelWeakPtr.Get())
            {
                return false;
            }
            // Only the component is restored; never replay the owner's whole object snapshot.
            Snapshot.BeginOperation();
            Snapshot.Apply();
            Snapshot.EndOperation();
            Owner = OwnerWeakPtr.Get();
            if (!IsValid(Component) || !IsValid(Owner) || Owner->IsActorBeingDestroyed()
                || Component->GetOwner() != Owner || Owner->GetWorld() != InWorld
                || Owner->GetLevel() != LevelWeakPtr.Get())
            {
                return false;
            }
            Owner->AddInstanceComponent(Component);
            if (bRegistered && !Component->IsRegistered())
            {
                Component->RegisterComponent();
            }
            return IsLive(InWorld) && Component->IsRegistered() == bRegistered;
        }
    };

    bool IsNativeRecord(UWandererAudioSourceComponent* InComponent)
    {
        if (!IsValid(InComponent) || !IsValid(InComponent->GetOwner()))
        {
            return false;
        }
        return (InComponent->GetClass() == UWandererAudioSourceComponent::StaticClass()
                && InComponent->GetOwner()->IsA<AAkSpatialAudioVolume>())
               || (InComponent->GetClass() == UWandererBuildingAmbienceComponent::StaticClass()
                   && InComponent->GetOwner()->IsA<AWandererWorldAudioBudget>());
    }

    bool IsContextLive(const TWeakObjectPtr<UWorld>& InWorldWeakPtr,
                       const TFunction<bool()>& InCanContinue)
    {
        if (InCanContinue && !InCanContinue())
        {
            return false;
        }
        return GEditor && !GEditor->PlayWorld && InWorldWeakPtr.IsValid()
               && !InWorldWeakPtr->bIsTearingDown
               && GEditor->GetEditorWorldContext().World() == InWorldWeakPtr.Get();
    }
} // namespace

bool FAudioComponentDeletionOperation::Execute(
    const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InComponentsWeakPtr,
    UWorld* InWorld, FString& OutError, TFunction<bool()> InCanContinue)
{
    OutError.Reset();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = InWorld;
    if (!IsInGameThread() || !GEditor || GIsTransacting || GEditor->IsTransactionActive()
        || !IsContextLive(WorldWeakPtr, InCanContinue) || InComponentsWeakPtr.IsEmpty())
    {
        OutError = TEXT("未删除：编辑上下文失效、选择为空或已有活动事务。");
        return false;
    }
    TArray<TUniquePtr<FDeletedComponent>> Records;
    TSet<UWandererAudioSourceComponent*> Seen;
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& ComponentWeakPtr :
         InComponentsWeakPtr)
    {
        UWandererAudioSourceComponent* Component = ComponentWeakPtr.Get();
        if (!IsNativeRecord(Component) || Seen.Contains(Component))
        {
            OutError = TEXT("未删除：目标失效、重复或不是原生 Room/Building 实例记录。");
            return false;
        }
        Seen.Add(Component);
        TUniquePtr<FDeletedComponent> Record = MakeUnique<FDeletedComponent>();
        Record->Component = Component;
        Record->OwnerWeakPtr = Component->GetOwner();
        Record->LevelWeakPtr = Component->GetOwner()->GetLevel();
        Record->bRegistered = Component->IsRegistered();
        if (!Record->IsLive(InWorld))
        {
            OutError = TEXT("未删除：目标归属改变或关卡锁定。");
            return false;
        }
        Records.Add(MoveTemp(Record));
    }
    FScopedTransaction Transaction(
        NSLOCTEXT("WorldAudio", "DeleteRecords", "Delete audio records"));
    for (const TUniquePtr<FDeletedComponent>& Record : Records)
    {
        if (!IsContextLive(WorldWeakPtr, InCanContinue) || !Record->IsLive(WorldWeakPtr.Get()))
        {
            OutError = TEXT("删除中止：编辑上下文或目标发生变化。");
            break;
        }
        Record->Component->SetFlags(RF_Transactional);
        Record->OwnerWeakPtr->Modify();
        if (!Record->IsLive(WorldWeakPtr.Get()))
        {
            OutError = TEXT("删除中止：记录归属在通知中发生变化。");
            break;
        }
        Record->Component->Modify();
        if (!IsContextLive(WorldWeakPtr, InCanContinue) || !Record->IsLive(WorldWeakPtr.Get()))
        {
            OutError = TEXT("删除中止：提交通知使目标失效。");
            break;
        }
        Record->Snapshot.SaveObject(Record->Component);
        Record->bDeleted = true;
        Record->Component->DestroyComponent();
    }
    if (OutError.IsEmpty() && IsContextLive(WorldWeakPtr, InCanContinue))
    {
        return true;
    }
    bool bRestored = true;
    for (int32 Index = Records.Num() - 1; Index >= 0; --Index)
    {
        bRestored = Records[Index]->Restore(WorldWeakPtr.Get()) && bRestored;
    }
    OutError += bRestored ? TEXT(" 本批组件已恢复，先前 Undo 保留。")
                          : TEXT(" 部分组件无法恢复；保留本批 Undo，请检查场景后撤销。");
    if (bRestored)
    {
        Transaction.Cancel();
    }
    return false;
}
