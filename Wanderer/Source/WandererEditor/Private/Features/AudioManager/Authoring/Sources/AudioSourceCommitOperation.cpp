#include "Features/AudioManager/Authoring/Sources/AudioSourceCommitOperation.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "ScopedTransaction.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Actors/Audio/WandererWorldAudioEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Sources/AudioSourceEditPlanBuilder.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

namespace
{
    constexpr float InitialAreaCellSizeCm = 600.0f;

    UClass* GetEmitterClass(EWandererWorldAudioMode InMode)
    {
        if (InMode == EWandererWorldAudioMode::Area)
        {
            return AWandererAudioAreaEmitter::StaticClass();
        }
        if (InMode == EWandererWorldAudioMode::Spline)
        {
            return AWandererAudioSplineEmitter::StaticClass();
        }
        return AWandererWorldAudioEmitter::StaticClass();
    }
}

FAudioSourceCommitOperation::FAudioSourceCommitOperation(
    const FAudioSourceRequest& InRequest, const FAudioSourcePlan& InPlan)
    : Request(InRequest), Plan(InPlan)
{
}

void FAudioSourceCommitOperation::Execute(FAudioSourceResult& InOutResult)
{
    if (!FAudioSourceEditPlanBuilder::CanContinue(Request) || !Plan.Table.IsValid()
        || GEditor->IsTransactionActive())
    {
        InOutResult.Errors.Add(TEXT("编辑上下文失效或已有事务，未提交声源修改。"));
        return;
    }
    FScopedTransaction Transaction(FText::FromString(Request.Action == EAudioSourceAction::Create
        ? TEXT("Create selected audio sources") : TEXT("Edit selected audio sources")));
    for (const FAudioSourcePlanItem& Item : Plan.Items)
    {
        if (!CommitItem(Item))
        {
            Failure = Item.Label + TEXT(": ") + Failure;
            break;
        }
    }
    if (!Failure.IsEmpty() || !ValidateCommitted())
    {
        InOutResult.Errors.Add(Failure.IsEmpty() ? TEXT("提交期间上下文或配置改变。") : Failure);
        Rollback(InOutResult);
        // Cancel discards only this transaction's records; compensation above restores the data.
        Transaction.Cancel();
        InOutResult.Skipped += Plan.Items.Num();
        return;
    }
    InOutResult.Applied += Plan.Items.Num();
}

bool FAudioSourceCommitOperation::CommitItem(const FAudioSourcePlanItem& InItem)
{
    Failure = FAudioSourceEditPlanBuilder::ValidateItem(Request, InItem);
    if (!Failure.IsEmpty())
    {
        return false;
    }
    UWandererAudioSourceComponent* Source =
        Request.Action == EAudioSourceAction::Create && !InItem.bReuseExistingSource
        ? CreateSource(InItem) : InItem.SourceWeakPtr.Get();
    if (!IsSourceLive(Source))
    {
        Failure = TEXT("创建/解析声源失败，或编辑上下文已改变。");
        return false;
    }
    return CommitProfile(InItem, *Source);
}

UWandererAudioSourceComponent* FAudioSourceCommitOperation::CreateSource(
    const FAudioSourcePlanItem& InItem)
{
    UWandererAudioSourceComponent* Source = InItem.Profile.Mode == EWandererWorldAudioMode::Room
        ? AttachRoomSource(InItem) : SpawnSource(InItem);
    if (!IsSourceLive(Source) || !Source->IsRegistered())
    {
        return nullptr;
    }
    Source->ResourceGroup = InItem.ResourceGroup;
    Source->Area = InItem.RoomWeakPtr.IsValid() ? FName(TEXT("Interior")) : NAME_None;
    Source->AcousticRoom = InItem.RoomWeakPtr.Get();
    Source->bEnabled = InItem.Profile.Mode != EWandererWorldAudioMode::Area
        && InItem.Profile.Mode != EWandererWorldAudioMode::Spline;
    return Source;
}

UWandererAudioSourceComponent* FAudioSourceCommitOperation::AttachRoomSource(
    const FAudioSourcePlanItem& InItem)
{
    AAkSpatialAudioVolume* Room = InItem.RoomWeakPtr.Get();
    if (!IsValid(Room))
    {
        return nullptr;
    }
    Room->Modify();
    UWandererAudioSourceComponent* Source =
        NewObject<UWandererAudioSourceComponent>(Room, NAME_None, RF_Transactional);
    CreatedComponentsWeakPtr.Add(Source);
    Source->AcousticRoom = Room;
    Room->AddInstanceComponent(Source);
    const TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr = Source;
    Source->OnComponentCreated();
    if (!IsSourceLive(SourceWeakPtr))
    {
        return nullptr;
    }
    SourceWeakPtr->RegisterComponent();
    return SourceWeakPtr.Get();
}

UWandererAudioSourceComponent* FAudioSourceCommitOperation::SpawnSource(
    const FAudioSourcePlanItem& InItem)
{
    AActor* Target = InItem.TargetWeakPtr.Get();
    FActorSpawnParameters Params;
    Params.OverrideLevel = Target->GetLevel();
    Params.ObjectFlags |= RF_Transactional;
    Target->GetLevel()->Modify();
    AActor* Actor = Request.World->SpawnActor<AActor>(GetEmitterClass(InItem.Profile.Mode),
        Target->GetActorLocation(), FRotator::ZeroRotator, Params);
    if (!IsValid(Actor))
    {
        return nullptr;
    }
    CreatedActorsWeakPtr.Add(Actor);
    UWandererAudioSourceComponent* Source =
        Actor->FindComponentByClass<UWandererAudioSourceComponent>();
    if (!IsSourceLive(Source))
    {
        return nullptr;
    }
    const TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr = Source;
    if (AWandererAudioAreaEmitter* Area = Cast<AWandererAudioAreaEmitter>(Actor))
    {
        Area->SetSurfaceCells({FVector::ZeroVector});
        Area->CellSize = InitialAreaCellSizeCm;
    }
    if (!IsSourceLive(SourceWeakPtr))
    {
        return nullptr;
    }
    const TWeakObjectPtr<AActor> ActorWeakPtr = Actor;
    Actor->SetActorLabel(TEXT("AUDIO__") + InItem.Label);
    if (!ActorWeakPtr.IsValid() || !IsSourceLive(SourceWeakPtr))
    {
        return nullptr;
    }
    Actor->SetFolderPath(TEXT("Audio/Sources"));
    return SourceWeakPtr.Get();
}

bool FAudioSourceCommitOperation::CommitProfile(const FAudioSourcePlanItem& InItem,
    UWandererAudioSourceComponent& InSource)
{
    const TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr = &InSource;
    FName CreatedName;
    const FDataTableRowHandle Handle = FWorldAudioProfileRepository::FindOrAdd(
        Plan.Table.Get(), InItem.Profile, InItem.Profile.PlayEvent.Get(),
        InItem.ProfileRowName, CreatedName);
    if (!CreatedName.IsNone())
    {
        ProfileChanges.Add({CreatedName, InItem.Profile});
    }
    if (Handle.IsNull() || !IsSourceLive(SourceWeakPtr))
    {
        Failure = TEXT("配置行写入失败或回调取消了编辑。");
        return false;
    }
    UWandererAudioSourceComponent* Source = SourceWeakPtr.Get();
    Failure = FAudioSourceEditPlanBuilder::ValidateItem(Request, InItem,
        Request.Action == EAudioSourceAction::Create ? Source : nullptr);
    if (!Failure.IsEmpty())
    {
        return false;
    }
    if (Request.Action == EAudioSourceAction::Edit || InItem.bReuseExistingSource)
    {
        RememberSource(*Source, Handle);
    }
    Source->SetFlags(RF_Transactional);
    Source->Modify();
    Source->Profile = Handle;
    CommittedSources.Add({Source, Handle, InItem.Profile});
    if (!PrepareRoom(InItem) || !IsSourceLive(SourceWeakPtr))
    {
        Failure = TEXT("Room 配置回调使编辑失效。");
        return false;
    }
    SourceWeakPtr->PostEditChange();
    if (!ValidateCommitted())
    {
        Failure = TEXT("编辑通知改变了提交结果或取消了操作。");
        return false;
    }
    return true;
}

void FAudioSourceCommitOperation::RememberSource(
    UWandererAudioSourceComponent& InSource, const FDataTableRowHandle& InHandle)
{
    FSourceChange& Change = SourceChanges.AddDefaulted_GetRef();
    Change.SourceWeakPtr = &InSource;
    Change.PreviousProfile = InSource.Profile;
    Change.WrittenProfile = InHandle;
    Change.PreviousTable.Reset(const_cast<UDataTable*>(InSource.Profile.DataTable.Get()));
}

bool FAudioSourceCommitOperation::PrepareRoom(const FAudioSourcePlanItem& InItem)
{
    if (InItem.Profile.Mode != EWandererWorldAudioMode::Room)
    {
        return true;
    }
    AAkSpatialAudioVolume* Room = InItem.RoomWeakPtr.Get();
    if (!IsValid(Room) || !IsValid(Room->Room))
    {
        return false;
    }
    if (!Room->Room->AkAudioEvent)
    {
        FRoomChange& Change = RoomChanges.AddDefaulted_GetRef();
        Change.RoomWeakPtr = Room->Room;
        Change.PreviousEvent.Reset(Room->Room->AkAudioEvent);
        Change.WrittenEventWeakPtr = InItem.Profile.PlayEvent.Get();
        Room->Room->SetFlags(RF_Transactional);
        Room->Room->Modify();
        Room->Room->AkAudioEvent = InItem.Profile.PlayEvent.Get();
        Room->Room->PostEditChange();
    }
    return FAudioSourceEditPlanBuilder::CanContinue(Request);
}

bool FAudioSourceCommitOperation::ValidateCommitted() const
{
    if (!FAudioSourceEditPlanBuilder::CanContinue(Request) || !Plan.Table.IsValid())
    {
        return false;
    }
    for (const FCommittedSource& Entry : CommittedSources)
    {
        if (!IsSourceLive(Entry.SourceWeakPtr)
            || Entry.SourceWeakPtr->Profile != Entry.Handle
            || !FWorldAudioProfileRepository::Matches(
                *Plan.Table, Entry.Handle.RowName, Entry.Profile))
        {
            return false;
        }
    }
    for (const FRoomChange& Change : RoomChanges)
    {
        if (!Change.RoomWeakPtr.IsValid()
            || Change.RoomWeakPtr->AkAudioEvent != Change.WrittenEventWeakPtr.Get())
        {
            return false;
        }
    }
    return true;
}

bool FAudioSourceCommitOperation::IsSourceLive(
    const TWeakObjectPtr<UWandererAudioSourceComponent>& InSourceWeakPtr) const
{
    if (!FAudioSourceEditPlanBuilder::CanContinue(Request))
    {
        return false;
    }
    const UWandererAudioSourceComponent* Source = InSourceWeakPtr.Get();
    return IsValid(Source) && IsValid(Source->GetOwner())
        && !Source->GetOwner()->IsActorBeingDestroyed()
        && Source->GetWorld() == Request.World.Get();
}
