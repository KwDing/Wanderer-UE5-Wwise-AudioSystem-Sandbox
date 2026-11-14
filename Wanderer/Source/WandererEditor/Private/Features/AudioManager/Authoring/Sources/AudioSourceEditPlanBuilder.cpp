#include "Features/AudioManager/Authoring/Sources/AudioSourceEditPlanBuilder.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "UObject/UnrealType.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"

bool FAudioSourceEditPlanBuilder::Build(const FAudioSourceRequest& InRequest,
    FAudioSourcePlan& OutPlan, FAudioSourceResult& InOutResult)
{
    OutPlan = FAudioSourcePlan();
    if (!CanContinue(InRequest))
    {
        InOutResult.Errors.Add(TEXT("编辑世界或窗口已失效，请重新选择。"));
        return false;
    }
    OutPlan.Table.Reset(Cast<UDataTable>(InRequest.TablePath.TryLoad()));
    if (!OutPlan.Table.IsValid()
        || OutPlan.Table->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        InOutResult.Errors.Add(TEXT("请选择有效的 World Audio 配置表。"));
        return false;
    }
    if (InRequest.Action == EAudioSourceAction::Edit
        && !InRequest.bApplyAll && InRequest.ChangedFields.IsEmpty())
    {
        InOutResult.Errors.Add(TEXT("未修改字段；如需统一全部值，请勾选应用全部配置。"));
        return false;
    }
    if (InRequest.Action == EAudioSourceAction::Create)
    {
        CollectCreateItems(InRequest, OutPlan, InOutResult);
    }
    else
    {
        CollectEditItems(InRequest, OutPlan, InOutResult);
    }
    const FString Error = AssignProfileNames(InRequest, OutPlan);
    if (!Error.IsEmpty() || !CanContinue(InRequest))
    {
        InOutResult.Errors.Add(Error.IsEmpty() ? TEXT("加载期间编辑上下文已失效。") : Error);
        return false;
    }
    return !OutPlan.Items.IsEmpty();
}

bool FAudioSourceEditPlanBuilder::CanContinue(const FAudioSourceRequest& InRequest)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld || !InRequest.World.IsValid())
    {
        return false;
    }
    if (InRequest.CanContinue && !InRequest.CanContinue())
    {
        return false;
    }
    UWorld* World = InRequest.World.Get();
    return IsValid(World) && World->WorldType == EWorldType::Editor
        && World == GEditor->GetEditorWorldContext().World();
}

FString FAudioSourceEditPlanBuilder::ValidateItem(const FAudioSourceRequest& InRequest,
    const FAudioSourcePlanItem& InItem,
    const UWandererAudioSourceComponent* InCreatedSource)
{
    if (!CanContinue(InRequest))
    {
        return TEXT("编辑上下文已失效。");
    }
    AActor* Target = InItem.TargetWeakPtr.Get();
    if (!IsValid(Target) || Target->IsActorBeingDestroyed()
        || Target->GetWorld() != InRequest.World.Get())
    {
        return TEXT("目标已销毁或不属于当前编辑世界。");
    }
    if (FLevelUtils::IsLevelLocked(Target->GetLevel()))
    {
        return TEXT("目标关卡锁定。");
    }
    if (InItem.Profile.Mode == EWandererWorldAudioMode::Room)
    {
        const FString Error = ValidateRoom(InItem.RoomWeakPtr.Get());
        if (!Error.IsEmpty())
        {
            return Error;
        }
    }
    return InRequest.Action == EAudioSourceAction::Create && !InItem.bReuseExistingSource
        ? ValidateCreate(InItem, InCreatedSource) : ValidateEdit(InItem);
}

TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> FAudioSourceEditPlanBuilder::GatherSources(
    UWorld* InWorld, const TArray<TWeakObjectPtr<AActor>>& InTargets)
{
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources;
    if (!IsValid(InWorld))
    {
        return Sources;
    }
    for (const TWeakObjectPtr<AActor>& TargetWeakPtr : InTargets)
    {
        AActor* Target = TargetWeakPtr.Get();
        if (!IsValid(Target) || Target->GetWorld() != InWorld)
        {
            continue;
        }
        TArray<UWandererAudioSourceComponent*> Direct;
        Target->GetComponents(Direct);
        for (UWandererAudioSourceComponent* Source : Direct)
        {
            Sources.AddUnique(Source);
        }
        if (AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(Target))
        {
            GatherRoomSources(*Room, Sources);
        }
    }
    return Sources;
}

FWandererWorldAudioProfileRow FAudioSourceEditPlanBuilder::MergeProfile(
    const FWandererWorldAudioProfileRow& InOriginal, const FAudioSourceRequest& InRequest)
{
    if (InRequest.bApplyAll)
    {
        return InRequest.Profile;
    }
    FWandererWorldAudioProfileRow Row = InOriginal;
    for (TFieldIterator<FProperty> It(FWandererWorldAudioProfileRow::StaticStruct()); It; ++It)
    {
        if (InRequest.ChangedFields.Contains(It->GetFName()))
        {
            It->CopyCompleteValue_InContainer(&Row, &InRequest.Profile);
        }
    }
    return Row;
}

void FAudioSourceEditPlanBuilder::CollectCreateItems(const FAudioSourceRequest& InRequest,
    FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult)
{
    TSet<TWeakObjectPtr<AActor>> Seen;
    for (const TWeakObjectPtr<AActor>& TargetWeakPtr : InRequest.Targets)
    {
        AActor* Target = TargetWeakPtr.Get();
        if (!IsValid(Target) || Seen.Contains(TargetWeakPtr))
        {
            ++InOutResult.Skipped;
            continue;
        }
        Seen.Add(TargetWeakPtr);
        FAudioSourcePlanItem Item;
        Item.TargetWeakPtr = Target;
        Item.RoomWeakPtr = Cast<AAkSpatialAudioVolume>(Target);
        Item.Profile = InRequest.Profile;
        Item.Label = Target->GetActorLabel();
        UWandererAudioSourceComponent* ShapeSource = nullptr;
        if (const AWandererAudioAreaEmitter* Area = Cast<AWandererAudioAreaEmitter>(Target))
        {
            ShapeSource = Area->AudioSource;
            Item.Profile.Mode = EWandererWorldAudioMode::Area;
        }
        else if (const AWandererAudioSplineEmitter* Spline = Cast<AWandererAudioSplineEmitter>(Target))
        {
            ShapeSource = Spline->AudioSource;
            Item.Profile.Mode = EWandererWorldAudioMode::Spline;
        }
        if (IsValid(ShapeSource) && ShapeSource->Profile.IsNull())
        {
            Item.bReuseExistingSource = true;
            Item.SourceWeakPtr = ShapeSource;
            Item.OriginalHandle = ShapeSource->Profile;
        }
        FindRoomGroup(Item.RoomWeakPtr.Get(), Item.ResourceGroup);
        AddValidatedItem(InRequest, MoveTemp(Item), InOutPlan, InOutResult);
    }
}

void FAudioSourceEditPlanBuilder::CollectEditItems(const FAudioSourceRequest& InRequest,
    FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult)
{
    TSet<TWeakObjectPtr<UWandererAudioSourceComponent>> Seen;
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& SourceWeakPtr : InRequest.Sources)
    {
        UWandererAudioSourceComponent* Source = SourceWeakPtr.Get();
        if (!IsValid(Source) || !IsValid(Source->GetOwner()) || Seen.Contains(SourceWeakPtr))
        {
            ++InOutResult.Skipped;
            continue;
        }
        Seen.Add(SourceWeakPtr);
        FAudioSourcePlanItem Item;
        Item.SourceWeakPtr = Source;
        Item.TargetWeakPtr = Source->GetOwner();
        Item.RoomWeakPtr = Source->ResolveRoom();
        Item.Label = Source->GetOwner()->GetActorLabel();
        Item.OriginalHandle = Source->Profile;
        const FWandererWorldAudioProfileRow* Original = Source->GetProfile();
        if (Original)
        {
            Item.OriginalProfile = *Original;
        }
        Item.Profile = Original ? MergeProfile(*Original, InRequest) : InRequest.Profile;
        if (!Original && !Source->GeometryImportSource.IsEmpty())
        {
            Item.Profile.Mode = Source->GetOwner()->IsA<AWandererAudioAreaEmitter>()
                ? EWandererWorldAudioMode::Area : EWandererWorldAudioMode::Spline;
        }
        if (!Original && !InRequest.bApplyAll && Source->GeometryImportSource.IsEmpty())
        {
            ++InOutResult.Skipped;
            InOutResult.Errors.Add(Item.Label + TEXT(": 配置缺失，请使用应用全部配置。"));
            continue;
        }
        AddValidatedItem(InRequest, MoveTemp(Item), InOutPlan, InOutResult);
    }
}

void FAudioSourceEditPlanBuilder::AddValidatedItem(const FAudioSourceRequest& InRequest,
    FAudioSourcePlanItem InItem, FAudioSourcePlan& InOutPlan, FAudioSourceResult& InOutResult)
{
    FString Error = ValidateItem(InRequest, InItem);
    if (Error.IsEmpty())
    {
        Error = LoadProfileEvents(InItem.Profile, InOutPlan);
    }
    if (Error.IsEmpty())
    {
        Error = ValidateItem(InRequest, InItem);
    }
    if (!Error.IsEmpty())
    {
        ++InOutResult.Skipped;
        InOutResult.Errors.Add(InItem.Label + TEXT(": ") + Error);
        return;
    }
    InOutPlan.Items.Add(MoveTemp(InItem));
}

FString FAudioSourceEditPlanBuilder::LoadProfileEvents(
    const FWandererWorldAudioProfileRow& InRow, FAudioSourcePlan& InOutPlan)
{
    const FString Error = InRow.Validate();
    if (!Error.IsEmpty())
    {
        return Error;
    }
    UAkAudioEvent* Event = InRow.PlayEvent.LoadSynchronous();
    if (!IsValid(Event))
    {
        return TEXT("Play Event 无法加载。");
    }
    InOutPlan.LoadedEvents.Emplace(Event);
    if (!InRow.NightPlayEvent.IsNull())
    {
        UAkAudioEvent* Night = InRow.NightPlayEvent.LoadSynchronous();
        if (!IsValid(Night))
        {
            return TEXT("Night Event 无法加载。");
        }
        InOutPlan.LoadedEvents.Emplace(Night);
    }
    const bool bShape = InRow.Mode == EWandererWorldAudioMode::Area
        || InRow.Mode == EWandererWorldAudioMode::Spline;
    if (bShape && (!FMath::IsFinite(InRow.AreaSpreadRadiusM) || InRow.AreaSpreadRadiusM <= 0))
    {
        return TEXT("Area Spread Radius 必须大于0。");
    }
    return FString();
}

FString FAudioSourceEditPlanBuilder::AssignProfileNames(const FAudioSourceRequest& InRequest,
    FAudioSourcePlan& InOutPlan)
{
    if (InRequest.ProfileRowName.IsNone())
    {
        return FString();
    }
    const FString NameError =
        WorldAudioProfileUtils::ValidateRowName(InRequest.ProfileRowName.ToString());
    if (!NameError.IsEmpty())
    {
        return NameError;
    }
    if (InOutPlan.Items.IsEmpty())
    {
        return FString();
    }
    const FString Conflict = WorldAudioProfileUtils::ValidateRow(InOutPlan.Table.Get(),
        InRequest.ProfileRowName, InOutPlan.Items[0].Profile);
    if (!Conflict.IsEmpty())
    {
        return Conflict;
    }

    TArray<const FAudioSourcePlanItem*> NamedProfiles;
    constexpr int32 FirstDuplicateSuffix = 2;
    int32 NextSuffix = FirstDuplicateSuffix;
    for (FAudioSourcePlanItem& Item : InOutPlan.Items)
    {
        const FAudioSourcePlanItem* const* Match =
            NamedProfiles.FindByPredicate([&Item](const FAudioSourcePlanItem* InNamed)
            {
                return FWandererWorldAudioProfileRow::StaticStruct()
                    ->CompareScriptStruct(&InNamed->Profile, &Item.Profile, 0);
            });
        if (Match)
        {
            Item.ProfileRowName = (*Match)->ProfileRowName;
            continue;
        }

        FName Name = InRequest.ProfileRowName;
        if (!NamedProfiles.IsEmpty())
        {
            const FString Error =
                FindAvailableProfileName(*InOutPlan.Table, Name, NextSuffix, Name);
            if (!Error.IsEmpty())
            {
                return Error;
            }
        }
        Item.ProfileRowName = Name;
        NamedProfiles.Add(&Item);
    }
    return FString();
}

FString FAudioSourceEditPlanBuilder::FindAvailableProfileName(const UDataTable& InTable,
    FName InBase, int32& InOutSuffix, FName& OutName)
{
    const FString Base = InBase.ToString();
    do
    {
        const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, InOutSuffix++);
        const FString Error = WorldAudioProfileUtils::ValidateRowName(Candidate);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        OutName = FName(*Candidate);
    } while (InTable.GetRowMap().Contains(OutName));
    return FString();
}

FString FAudioSourceEditPlanBuilder::ValidateRoom(AAkSpatialAudioVolume* InRoom)
{
    if (!IsValid(InRoom) || !IsValid(InRoom->Room))
    {
        return TEXT("Room 模式需要有效的 Acoustic Room。");
    }
    if (FLevelUtils::IsLevelLocked(InRoom->GetLevel()))
    {
        return TEXT("Room 关卡锁定。");
    }
    if (!InRoom->Room->bEnable || InRoom->Room->AutoPost)
    {
        return TEXT("Room 需要启用并关闭 Auto Post，避免重复播放。");
    }
    return FString();
}

FString FAudioSourceEditPlanBuilder::FindRoomGroup(AAkSpatialAudioVolume* InRoom, FName& OutGroup)
{
    OutGroup = NAME_None;
    if (!IsValid(InRoom))
    {
        return FString();
    }
    int32 Memberships = 0;
    for (TActorIterator<AWandererWorldAudioBudget> It(InRoom->GetWorld()); It; ++It)
    {
        for (const FWandererAudioResourceGroup& Group : It->ResourceGroups)
        {
            if (Group.Rooms.ContainsByPredicate([InRoom](const auto& Member)
                {
                    return Member.Get() == InRoom;
                }))
            {
                OutGroup = Group.Name;
                ++Memberships;
            }
        }
    }
    return Memberships > 1 ? TEXT("Room 属于多个资源组。") : FString();
}

FString FAudioSourceEditPlanBuilder::ValidateCreate(const FAudioSourcePlanItem& InItem,
    const UWandererAudioSourceComponent* InCreatedSource)
{
    AActor* Target = InItem.TargetWeakPtr.Get();
    if (Target->IsA<AAkSpatialAudioVolume>()
        && InItem.Profile.Mode != EWandererWorldAudioMode::Room)
    {
        return TEXT("Acoustic Room 只直接添加 Room 底噪。");
    }
    const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Sources =
        GatherSources(Target->GetWorld(), {Target});
    if (Sources.ContainsByPredicate([InCreatedSource](const auto& SourceWeakPtr)
        {
            return SourceWeakPtr.Get() != InCreatedSource;
        }))
    {
        return TEXT("已有关联声源，请使用修改入口。");
    }
    FName Group;
    const FString Error = FindRoomGroup(InItem.RoomWeakPtr.Get(), Group);
    if (!Error.IsEmpty())
    {
        return Error;
    }
    return Group == InItem.ResourceGroup ? FString() : TEXT("资源组已改变，请重新提交。");
}

FString FAudioSourceEditPlanBuilder::ValidateEdit(const FAudioSourcePlanItem& InItem)
{
    UWandererAudioSourceComponent* Source = InItem.SourceWeakPtr.Get();
    AActor* Owner = InItem.TargetWeakPtr.Get();
    if (!IsValid(Source) || Source->GetOwner() != Owner
        || Source->IsA<UWandererBuildingAmbienceComponent>())
    {
        return TEXT("声源失效，或需要在建筑页编辑。");
    }
    const FWandererWorldAudioProfileRow* Original = Source->GetProfile();
    if (Source->Profile != InItem.OriginalHandle
        || bool(Original) != InItem.OriginalProfile.IsSet())
    {
        return TEXT("声源配置引用已改变，请重新提交。");
    }
    if (Original && !FWandererWorldAudioProfileRow::StaticStruct()->CompareScriptStruct(
            Original, &InItem.OriginalProfile.GetValue(), 0))
    {
        return TEXT("原配置内容已改变，请重新提交。");
    }
    if (Source->ResolveRoom() != InItem.RoomWeakPtr.Get())
    {
        return TEXT("关联 Room 已改变，请重新提交。");
    }
    const EWandererWorldAudioMode Mode = InItem.Profile.Mode;
    if (Owner->IsA<AAkSpatialAudioVolume>() && Mode != EWandererWorldAudioMode::Room)
    {
        return TEXT("Room 自带组件需保持 Room 模式。");
    }
    if (Mode == EWandererWorldAudioMode::Area && !Owner->IsA<AWandererAudioAreaEmitter>())
    {
        return TEXT("Area 需要面声源 Actor。");
    }
    if (Mode == EWandererWorldAudioMode::Spline && !Owner->IsA<AWandererAudioSplineEmitter>())
    {
        return TEXT("Spline 需要样条声源 Actor。");
    }
    return FString();
}

void FAudioSourceEditPlanBuilder::GatherRoomSources(AAkSpatialAudioVolume& InRoom,
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InOutSources)
{
    for (TActorIterator<AActor> It(InRoom.GetWorld()); It; ++It)
    {
        TArray<UWandererAudioSourceComponent*> Sources;
        It->GetComponents(Sources);
        for (UWandererAudioSourceComponent* Source : Sources)
        {
            if (Source->ResolveRoom() == &InRoom)
            {
                InOutSources.AddUnique(Source);
            }
        }
    }
}
