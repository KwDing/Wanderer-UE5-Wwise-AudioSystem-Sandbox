#include "Features/AudioManager/Authoring/Geometry/AudioGeometryPlanBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelUtils.h"

#include "AkAudioEvent.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    FString FindExistingAudioTarget(AActor& InSource, AActor*& OutTarget)
    {
        const FString SourcePath = InSource.GetPathName();
        for (TActorIterator<AActor> It(InSource.GetWorld()); It; ++It)
        {
            const UWandererAudioSourceComponent* Audio =
                It->FindComponentByClass<UWandererAudioSourceComponent>();
            if (!Audio || Audio->GeometryImportSource != SourcePath)
            {
                continue;
            }
            if (OutTarget || !Audio->Profile.IsNull())
            {
                return TEXT("IF 来源已有关联声源，请从列表修改或修复配对。");
            }
            OutTarget = *It;
        }
        return FString();
    }
}

bool FAudioGeometryPlanBuilder::CanContinue(const FAudioGeometryPlan& InPlan)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || GIsTransacting
        || (InPlan.CanContinue && !InPlan.CanContinue()))
    {
        return false;
    }
    UWorld* World = InPlan.WorldWeakPtr.Get();
    return IsValid(World) && !World->bIsTearingDown && World->WorldType == EWorldType::Editor
           && World == GEditor->GetEditorWorldContext().World();
}

FString FAudioGeometryPlanBuilder::Add(FAudioGeometryPlan& InOutPlan, AActor* InSource,
                                       AActor* InTarget, bool bInKeepTransform)
{
    const TWeakObjectPtr<AActor> SourceWeakPtr = InSource;
    const TWeakObjectPtr<AActor> TargetWeakPtr = InTarget;
    const bool bCreate = InTarget == nullptr;
    if (!CanContinue(InOutPlan)
        || !FInteriorFitGeometryImportOperation::IsSource(SourceWeakPtr.Get())
        || (!bCreate && !TargetWeakPtr.IsValid()))
    {
        return TEXT("编辑上下文或 IF 来源已失效。");
    }
    AActor* Source = SourceWeakPtr.Get();
    AActor* Target = TargetWeakPtr.Get();
    FAudioGeometryPlanItem Item;
    Item.SourceWeakPtr = SourceWeakPtr;
    Item.TargetWeakPtr = TargetWeakPtr;
    Item.TargetLevelWeakPtr = Target ? Target->GetLevel() : nullptr;
    Item.bCreate = bCreate;
    Item.SourcePath = Source->GetPathName();
    FString Error = AudioGeometryUtils::Read(Source, Item.Geometry);
    if (!Error.IsEmpty())
    {
        return Error;
    }
    Item.SourceDigest = AudioGeometryUtils::Digest(Item.Geometry);
    if (Target)
    {
        Error = ReadTarget(Item, *Target);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        if (bInKeepTransform)
        {
            Item.Geometry.Transform = Item.Previous.Transform;
        }
        if (!Item.Geometry.bArea)
        {
            Item.Geometry.Above = Item.Previous.Above;
            Item.Geometry.Below = Item.Previous.Below;
        }
    }
    constexpr double LakeUpZMin = 0.9999;
    if (!Item.Geometry.bArea && Item.Geometry.bClosed
        && Item.Geometry.Transform.GetRotation().GetUpVector().Z < LakeUpZMin)
    {
        return TEXT("Lake 只允许水平朝向，请使用 Line 处理倾斜路径。");
    }
    Item.Geometry.bSplineEdited = true;
    Item.Metadata.Source = Item.SourcePath;
    Item.Metadata.Group = FInteriorFitGeometryImportOperation::GetSourceGroup(*Source);
    Item.Metadata.Digest = Item.SourceDigest;
    if (!Item.Metadata.Batch.IsValid())
    {
        Item.Metadata.Batch = FGuid::NewGuid();
    }
    Error = Validate(InOutPlan, Item, true);
    if (Error.IsEmpty())
    {
        InOutPlan.Items.Add(MoveTemp(Item));
    }
    return Error;
}

FString FAudioGeometryPlanBuilder::Import(const FAudioSourceRequest& InRequest,
                                          FAudioGeometryPlan& OutPlan)
{
    OutPlan.WorldWeakPtr = InRequest.World;
    OutPlan.CanContinue = InRequest.CanContinue;
    OutPlan.ResourceGroup = InRequest.ResourceGroup;
    OutPlan.ProfileRowName = InRequest.ProfileRowName;
    if (!CanContinue(OutPlan) || InRequest.Targets.IsEmpty())
    {
        return TEXT("编辑世界已失效或未选择 IF 几何。");
    }
    OutPlan.Table.Reset(Cast<UDataTable>(InRequest.TablePath.TryLoad()));
    if (!OutPlan.Table.IsValid()
        || OutPlan.Table->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return TEXT("请选择 World Audio 配置表。");
    }
    const FGuid Batch = FGuid::NewGuid();
    TSet<TWeakObjectPtr<AActor>> Seen;
    for (const TWeakObjectPtr<AActor>& Source : InRequest.Targets)
    {
        if (Seen.Contains(Source))
        {
            return TEXT("重复来源，请重新选择。");
        }
        Seen.Add(Source);
        if (!FInteriorFitGeometryImportOperation::IsSource(Source.Get()))
        {
            return TEXT("IF 来源已失效，请重新选择。");
        }
        AActor* Existing = nullptr;
        const FString TargetError = FindExistingAudioTarget(*Source.Get(), Existing);
        if (!TargetError.IsEmpty())
        {
            return TargetError;
        }
        const FString Error = Add(OutPlan, Source.Get(), Existing);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        FAudioGeometryPlanItem& Item = OutPlan.Items.Last();
        Item.Metadata.Batch = Batch;
        Item.Metadata.Status = TEXT("IF 已同步");
        Item.bWriteProfile = true;
        Item.Profile = InRequest.Profile;
        Item.Profile.Mode
            = Item.Geometry.bArea ? EWandererWorldAudioMode::Area : EWandererWorldAudioMode::Spline;
        if (!OutPlan.ProfileRowName.IsNone())
        {
            const FString NameError = WorldAudioProfileUtils::ValidateRow(
                OutPlan.Table.Get(), OutPlan.ProfileRowName, Item.Profile);
            if (!NameError.IsEmpty())
            {
                return NameError;
            }
            if (Item.Profile.Mode != OutPlan.Items[0].Profile.Mode)
            {
                return TEXT("Area 与 Spline 需要不同配置行，请分别创建。");
            }
        }
        if (!Item.Profile.Validate().IsEmpty() || !FMath::IsFinite(Item.Profile.AreaSpreadRadiusM)
            || Item.Profile.AreaSpreadRadiusM <= 0)
        {
            return TEXT("声音配置无效；Area Spread Radius 必须大于 0。");
        }
        OutPlan.Events.Emplace(Item.Profile.PlayEvent.LoadSynchronous());
        if (!OutPlan.Events.Last().IsValid())
        {
            return TEXT("Play Event 无法加载。");
        }
        if (!Item.Profile.NightPlayEvent.IsNull())
        {
            OutPlan.Events.Emplace(Item.Profile.NightPlayEvent.LoadSynchronous());
            if (!OutPlan.Events.Last().IsValid())
            {
                return TEXT("Night Event 无法加载。");
            }
        }
    }
    return CanContinue(OutPlan) ? FString() : TEXT("加载期间编辑上下文已失效。");
}

FString FAudioGeometryPlanBuilder::Validate(const FAudioGeometryPlan& InPlan,
                                            const FAudioGeometryPlanItem& InItem,
                                            bool bInCheckTarget)
{
    if (!CanContinue(InPlan))
    {
        return TEXT("编辑上下文已失效。");
    }
    for (AActor* Actor : {InItem.SourceWeakPtr.Get(), InItem.TargetWeakPtr.Get()})
    {
        if (Actor
            && (Actor->IsActorBeingDestroyed() || Actor->GetWorld() != InPlan.WorldWeakPtr.Get()
                || FLevelUtils::IsLevelLocked(Actor->GetLevel())))
        {
            return TEXT("对象跨世界、已销毁或所在关卡锁定。");
        }
    }
    if (InItem.bWriteGeometry)
    {
        AActor* Source = InItem.SourceWeakPtr.Get();
        if (!FInteriorFitGeometryImportOperation::IsSource(Source)
            || Source->GetPathName() != InItem.SourcePath
            || FInteriorFitGeometryImportOperation::Fingerprint(Source) != InItem.SourceDigest)
        {
            return TEXT("来源在预检后改变，请重新操作。");
        }
    }
    if (!InItem.bCreate
        && (!InItem.TargetWeakPtr.IsValid()
            || InItem.TargetWeakPtr->GetLevel() != InItem.TargetLevelWeakPtr.Get()))
    {
        return TEXT("目标或其关卡在预检后改变。");
    }
    if (!InItem.bCreate && bInCheckTarget)
    {
        UWandererAudioSourceComponent* Audio = InItem.AudioWeakPtr.Get();
        FAudioGeometrySnapshot Current;
        if (!Audio || !InItem.TargetWeakPtr.IsValid()
            || Audio->GetOwner() != InItem.TargetWeakPtr.Get()
            || Audio->Profile != InItem.PreviousProfile
            || !AudioGeometryUtils::Matches(AudioGeometryUtils::ReadMetadata(*Audio),
                                            InItem.PreviousMetadata))
        {
            return TEXT("声源或同步信息在预检后改变。");
        }
        if (InItem.bWriteGeometry
            && (!AudioGeometryUtils::Read(InItem.TargetWeakPtr.Get(), Current).IsEmpty()
                || !AudioGeometryUtils::MatchesGeometry(Current, InItem.Previous)))
        {
            return TEXT("目标形状在预检后改变。");
        }
    }
    return FString();
}

FString FAudioGeometryPlanBuilder::Suspend(FAudioGeometryPlan& InOutPlan,
                                           UWandererAudioSourceComponent& InAudio,
                                           const FString& InStatus)
{
    FAudioGeometryPlanItem Item;
    Item.TargetWeakPtr = InAudio.GetOwner();
    Item.TargetLevelWeakPtr = InAudio.GetOwner()->GetLevel();
    Item.AudioWeakPtr = &InAudio;
    Item.PreviousMetadata = AudioGeometryUtils::ReadMetadata(InAudio);
    Item.Metadata = Item.PreviousMetadata;
    Item.PreviousProfile = InAudio.Profile;
    Item.bWriteGeometry = false;
    SetStatus(Item.Metadata, InStatus, true);
    if (!AudioGeometryUtils::Matches(Item.Metadata, Item.PreviousMetadata))
    {
        InOutPlan.Items.Add(MoveTemp(Item));
    }
    return FString();
}

void FAudioGeometryPlanBuilder::SetStatus(FAudioGeometryMetadata& InOutMetadata,
                                          const FString& InStatus, bool bInSuspend)
{
    if (bInSuspend && !InOutMetadata.bSuspended)
    {
        InOutMetadata.bWasEnabled = InOutMetadata.bEnabled;
        InOutMetadata.bEnabled = false;
    }
    if (!bInSuspend && InOutMetadata.bSuspended)
    {
        InOutMetadata.bEnabled = InOutMetadata.bWasEnabled;
    }
    InOutMetadata.bSuspended = bInSuspend;
    InOutMetadata.Status = InStatus;
}

FString FAudioGeometryPlanBuilder::ReadTarget(FAudioGeometryPlanItem& InOutItem, AActor& InTarget)
{
    if (!InTarget.IsA<AWandererAudioAreaEmitter>() && !InTarget.IsA<AWandererAudioSplineEmitter>())
    {
        return TEXT("目标不是 Wanderer 形状声源。");
    }
    InOutItem.AudioWeakPtr = InTarget.FindComponentByClass<UWandererAudioSourceComponent>();
    const FString Error = AudioGeometryUtils::Read(&InTarget, InOutItem.Previous);
    if (!Error.IsEmpty() || !InOutItem.AudioWeakPtr.IsValid()
        || InOutItem.Previous.bArea != InOutItem.Geometry.bArea)
    {
        return TEXT("目标几何无效或与来源类型不同。");
    }
    InOutItem.PreviousMetadata = AudioGeometryUtils::ReadMetadata(*InOutItem.AudioWeakPtr);
    InOutItem.PreviousProfile = InOutItem.AudioWeakPtr->Profile;
    InOutItem.Metadata = InOutItem.PreviousMetadata;
    return FString();
}
