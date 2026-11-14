#include "Features/AudioManager/Authoring/Geometry/AudioGeometryCommitOperation.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UnrealTemplate.h"

#include "AkAudioEvent.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryPlanBuilder.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

namespace
{
    bool bCommitActive = false;
}

FAudioGeometryCommitOperation::FAudioGeometryCommitOperation(const FAudioGeometryPlan& InPlan)
    : Plan(InPlan)
{
}

void FAudioGeometryCommitOperation::Execute(FAudioSourceResult& InOutResult)
{
    if (!FAudioGeometryPlanBuilder::CanContinue(Plan) || bCommitActive
        || GEditor->IsTransactionActive())
    {
        InOutResult.Errors.Add(TEXT("IF 提交上下文无效、发生重入或已有编辑事务。"));
        return;
    }
    const TGuardValue<bool> Guard(bCommitActive, true);
    for (const FAudioGeometryPlanItem& Item : Plan.Items)
    {
        Failure = FAudioGeometryPlanBuilder::Validate(Plan, Item, true);
        if (!Failure.IsEmpty())
        {
            InOutResult.Errors.Add(Failure);
            return;
        }
    }
    if (Plan.Items.IsEmpty())
    {
        return;
    }
    FScopedTransaction Transaction(FText::FromString(TEXT("Edit IF audio geometry")));
    for (const FAudioGeometryPlanItem& Item : Plan.Items)
    {
        if (!CommitItem(Item))
        {
            break;
        }
    }
    if (Failure.IsEmpty() && !ValidateCommitted())
    {
        Failure = TEXT("IF 提交后结果或编辑上下文改变。");
    }
    if (!Failure.IsEmpty())
    {
        InOutResult.Errors.Add(Failure);
        if (Rollback(InOutResult))
        {
            Transaction.Cancel();
        }
        return;
    }
    InOutResult.Applied = Changes.Num();
}

AActor* FAudioGeometryCommitOperation::GetCreatedActor() const
{
    return Failure.IsEmpty() && !Changes.IsEmpty() && Changes[0].Item.bCreate
               ? Changes[0].ActorWeakPtr.Get()
               : nullptr;
}

bool FAudioGeometryCommitOperation::CommitItem(const FAudioGeometryPlanItem& InItem)
{
    Failure = FAudioGeometryPlanBuilder::Validate(Plan, InItem, true);
    if (!Failure.IsEmpty())
    {
        return false;
    }
    FChange& Change = Changes.AddDefaulted_GetRef();
    Change.Item = InItem;
    Change.LevelWeakPtr
        = InItem.bCreate ? InItem.SourceWeakPtr->GetLevel() : InItem.TargetLevelWeakPtr.Get();
    Change.ActorWeakPtr = InItem.bCreate ? Spawn(InItem) : InItem.TargetWeakPtr.Get();
    const TStrongObjectPtr<AActor> Actor(Change.ActorWeakPtr.Get());
    if (!Actor.IsValid() || Actor->IsActorBeingDestroyed())
    {
        Failure = TEXT("创建/取得 IF 声源失败。");
        return false;
    }
    Change.RootWeakPtr = Actor->GetRootComponent();
    Change.AudioWeakPtr = Actor->FindComponentByClass<UWandererAudioSourceComponent>();
    const TStrongObjectPtr<UWandererAudioSourceComponent> Audio(Change.AudioWeakPtr.Get());
    if (!Audio.IsValid() || !FAudioGeometryPlanBuilder::CanContinue(Plan))
    {
        Failure = TEXT("声源组件或提交上下文已失效。");
        return false;
    }
    Change.WrittenProfile = Audio->Profile;
    if (InItem.bCreate)
    {
        Change.Item.PreviousMetadata = AudioGeometryUtils::ReadMetadata(*Audio);
    }
    if (!PrepareObjects(Change))
    {
        Failure = TEXT("记录事务期间对象或上下文改变。");
        return false;
    }
    if (InItem.bWriteGeometry)
    {
        Change.bGeometryWritten = true;
        if (!AudioGeometryUtils::Write(Actor.Get(), InItem.Geometry)
            || !AudioGeometryUtils::ValidateResult(Actor.Get()).IsEmpty())
        {
            Failure = TEXT("导入后的声源几何无效。");
            return false;
        }
    }
    return CompleteItem(Change);
}

bool FAudioGeometryCommitOperation::CompleteItem(FChange& InOutChange)
{
    const FAudioGeometryPlanItem& InItem = InOutChange.Item;
    const TStrongObjectPtr<AActor> Actor(InOutChange.ActorWeakPtr.Get());
    const TStrongObjectPtr<UWandererAudioSourceComponent> Audio(InOutChange.AudioWeakPtr.Get());
    if (InItem.bWriteProfile && !CommitProfile(InOutChange))
    {
        return false;
    }
    Failure = FAudioGeometryPlanBuilder::Validate(Plan, InItem, false);
    if (!Failure.IsEmpty() || !InOutChange.AudioWeakPtr.IsValid() || Actor->IsActorBeingDestroyed()
        || Audio->GetOwner() != Actor.Get())
    {
        Failure = TEXT("IF 提交期间来源或目标失效。");
        return false;
    }
    if (!MatchesBeforeMetadata(InOutChange))
    {
        Failure = TEXT("同步信息或配置被外部修改，停止提交。");
        return false;
    }
    InOutChange.bMetadataWritten = true;
    AudioGeometryUtils::WriteMetadata(*Audio, InItem.Metadata);
    if (!ValidateCommitted())
    {
        Failure = TEXT("同步通知改变了提交结果。");
        return false;
    }
    if (InItem.bCreate)
    {
        const TStrongObjectPtr<AActor> Source(InItem.SourceWeakPtr.Get());
        AudioGeometryUtils::SetLabel(*Actor, *Source, InItem.Geometry.bArea);
        if (IsValid(Actor.Get()) && !Actor->IsActorBeingDestroyed())
        {
            Actor->SetFolderPath(TEXT("Audio/Sources"));
        }
    }
    return true;
}

bool FAudioGeometryCommitOperation::PrepareObjects(FChange& InOutChange)
{
    AActor* Actor = InOutChange.ActorWeakPtr.Get();
    TArray<TWeakObjectPtr<UObject>> Objects;
    Objects.Add(Actor);
    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (UActorComponent* Component : Components)
    {
        Objects.Add(Component);
    }
    for (const TWeakObjectPtr<UObject>& Object : Objects)
    {
        const TStrongObjectPtr<UObject> Pin(Object.Get());
        if (!Pin.IsValid())
        {
            return false;
        }
        Pin->Modify();
        if (!MatchesBeforeMetadata(InOutChange)
            || !FAudioGeometryPlanBuilder::Validate(Plan, InOutChange.Item, true).IsEmpty())
        {
            return false;
        }
    }
    return true;
}

bool FAudioGeometryCommitOperation::MatchesBeforeMetadata(const FChange& InChange) const
{
    AActor* Actor = InChange.ActorWeakPtr.Get();
    UWandererAudioSourceComponent* Audio = InChange.AudioWeakPtr.Get();
    return FAudioGeometryPlanBuilder::CanContinue(Plan) && IsValid(Actor)
           && !Actor->IsActorBeingDestroyed() && IsValid(Audio) && Audio->GetOwner() == Actor
           && InChange.RootWeakPtr.IsValid()
           && Actor->GetRootComponent() == InChange.RootWeakPtr.Get()
           && Actor->GetWorld() == Plan.WorldWeakPtr.Get()
           && Actor->GetLevel() == InChange.LevelWeakPtr.Get()
           && !FLevelUtils::IsLevelLocked(Actor->GetLevel())
           && Audio->Profile == InChange.WrittenProfile
           && AudioGeometryUtils::Matches(AudioGeometryUtils::ReadMetadata(*Audio),
                                          InChange.Item.PreviousMetadata);
}

AActor* FAudioGeometryCommitOperation::Spawn(const FAudioGeometryPlanItem& InItem)
{
    if (!FAudioGeometryPlanBuilder::CanContinue(Plan))
    {
        return nullptr;
    }
    AActor* Source = InItem.SourceWeakPtr.Get();
    if (!IsValid(Source) || Source->IsActorBeingDestroyed())
    {
        return nullptr;
    }
    FActorSpawnParameters Params;
    Params.OverrideLevel = Source->GetLevel();
    Params.ObjectFlags |= RF_Transactional;
    const TWeakObjectPtr<ULevel> LevelWeakPtr = Params.OverrideLevel;
    Params.OverrideLevel->Modify();
    if (!FAudioGeometryPlanBuilder::Validate(Plan, InItem, true).IsEmpty()
        || !LevelWeakPtr.IsValid() || InItem.SourceWeakPtr->GetLevel() != LevelWeakPtr.Get())
    {
        return nullptr;
    }
    UClass* Class = InItem.Geometry.bArea ? AWandererAudioAreaEmitter::StaticClass()
                                          : AWandererAudioSplineEmitter::StaticClass();
    return Plan.WorldWeakPtr->SpawnActor<AActor>(Class, InItem.Geometry.Transform, Params);
}

bool FAudioGeometryCommitOperation::CommitProfile(FChange& InOutChange)
{
    FCreatedRow& Row = CreatedRows.AddDefaulted_GetRef();
    Row.Profile = InOutChange.Item.Profile;
    const FDataTableRowHandle Handle = FWorldAudioProfileRepository::FindOrAdd(
        Plan.Table.Get(), Row.Profile, Row.Profile.PlayEvent.Get(), Plan.ProfileRowName, Row.Name);
    // The journal slot exists before AddRow can notify external code.
    UWandererAudioSourceComponent* Audio = InOutChange.AudioWeakPtr.Get();
    if (Handle.IsNull() || !IsValid(Audio) || !FAudioGeometryPlanBuilder::CanContinue(Plan))
    {
        Failure = TEXT("创建配置行失败或编辑上下文已失效。");
        return false;
    }
    if (!MatchesBeforeMetadata(InOutChange))
    {
        Failure = TEXT("配置表通知改变了目标，未覆盖外部编辑。");
        return false;
    }
    Audio->Profile = Handle;
    if (InOutChange.Item.bCreate)
    {
        Audio->ResourceGroup = Plan.ResourceGroup;
        Audio->Area = Plan.ResourceGroup;
    }
    InOutChange.WrittenProfile = Handle;
    return true;
}

bool FAudioGeometryCommitOperation::ValidateCommitted() const
{
    if (!FAudioGeometryPlanBuilder::CanContinue(Plan))
    {
        return false;
    }
    for (const FChange& Change : Changes)
    {
        if (!Change.bMetadataWritten)
        {
            continue;
        }
        if (!FAudioGeometryPlanBuilder::Validate(Plan, Change.Item, false).IsEmpty())
        {
            return false;
        }
        AActor* Actor = Change.ActorWeakPtr.Get();
        UWandererAudioSourceComponent* Audio = Change.AudioWeakPtr.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || !IsValid(Audio)
            || Audio->GetOwner() != Actor || Audio->Profile != Change.WrittenProfile
            || !Change.RootWeakPtr.IsValid()
            || Actor->GetRootComponent() != Change.RootWeakPtr.Get()
            || Actor->GetWorld() != Plan.WorldWeakPtr.Get()
            || Actor->GetLevel() != Change.LevelWeakPtr.Get()
            || FLevelUtils::IsLevelLocked(Actor->GetLevel())
            || !AudioGeometryUtils::Matches(AudioGeometryUtils::ReadMetadata(*Audio),
                                            Change.Item.Metadata))
        {
            return false;
        }
        FAudioGeometrySnapshot Current;
        if (Change.Item.bWriteGeometry
            && (!AudioGeometryUtils::Read(Actor, Current).IsEmpty()
                || !AudioGeometryUtils::MatchesGeometry(Current, Change.Item.Geometry)))
        {
            return false;
        }
        if (Change.Item.bWriteProfile
            && !FWorldAudioProfileRepository::Matches(*Plan.Table, Change.WrittenProfile.RowName,
                                                      Change.Item.Profile))
        {
            return false;
        }
    }
    return true;
}
