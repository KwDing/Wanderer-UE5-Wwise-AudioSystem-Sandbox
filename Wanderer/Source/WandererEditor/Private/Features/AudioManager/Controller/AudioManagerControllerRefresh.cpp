#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "EngineUtils.h"

#include "AkAcousticPortal.h"
#include "AkAmbientSound.h"
#include "AkComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Core/ActorPathUtils.h"
#include "Features/AudioManager/AudioManagerData.h"
#include "Features/AudioManager/InteriorFitAudioSyncService.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

void FAudioManagerController::Refresh()
{
    if (bStopped || bRefreshing)
    {
        return;
    }
    const TSharedRef<FAudioManagerController> KeepAlive = AsShared();
    TGuardValue<bool> Guard(bRefreshing, true);
    const TArray<FEntryPtr> PreviousSelection = SelectedEntries;
    const TArray<FEntryPtr> PreviousEntries = State.Entries;
    State.Entries.Reset();
    State.ResourceConfigurationNotice.Reset();
    VisualizationController->RefreshSources();
    UWorld* World = GetObservedWorld();
    if (ObservedWorldWeakPtr.Get() != World)
    {
        SetDetailsObjects({});
        if (bStopped)
        {
            return;
        }
        ObservedWorldWeakPtr = World;
    }
    const TWeakObjectPtr<AWandererWorldAudioBudget> BudgetWeakPtr = FindBudget();
    UpdateGroupOptions(BudgetWeakPtr.Get());
    if (bStopped || GetObservedWorld() != World)
    {
        return;
    }
    AWandererWorldAudioBudget* Budget = BudgetWeakPtr.Get();
    if (State.bProjectAssets)
    {
        AddProjectAssets();
    }
    else if (State.ActivePage == 1)
    {
        if (Budget)
        {
            AddResourceEntries(*Budget);
        }
    }
    else if (World)
    {
        AddWorldEntries(*World, Budget);
    }
    RestoreEntries(PreviousEntries, PreviousSelection);
    RefreshTable();
    if (bStopped || GetObservedWorld() != World)
    {
        return;
    }
    if (State.ActivePage == 0 || State.ActivePage == 3)
    {
        UpdateSelectionDetails();
    }
    UpdateSummary();
}

void FAudioManagerController::UpdateSummary()
{
    if (bStopped)
    {
        return;
    }
    FString Text = FString::Printf(
        TEXT("%s · %d 行 | Ctrl/Shift 多选，点击表头排序；横向滚动查看更多列。PIE 只读。"),
        State.ActivePage == 1   ? TEXT("区域预算")
        : State.ActivePage == 3 ? TEXT("建筑底噪")
                                : TEXT("音频对象"),
        State.Entries.Num());
    if (State.bProjectAssets)
    {
        Text += TEXT("\n项目资产视图：音频 DT、Ak Event 与地图。未加载地图内部 Actor "
                     "不在此扫描；选择地图并打开后，勾选当前地图管理"
                     "其区域。不会自动加载所有地图。");
    }
    if (State.ActivePage == 1)
    {
        Text += TEXT("\n选择行后在右侧编辑地图 Resource Groups；共享参数点‘打开’。范围/Room/Portal "
                     "保存在地图，策略和区"
                     "域额度存 DT。0 = 不额外限制，保护声源可超额。");
    }
    if (State.ActivePage == 1 && !State.ResourceConfigurationNotice.IsEmpty())
    {
        Text += TEXT("\n") + State.ResourceConfigurationNotice;
    }
    if (State.ActivePage == 0 && !State.SharedRoomNotice.IsEmpty())
    {
        Text += TEXT("\n") + State.SharedRoomNotice;
    }
    if (UWorld* World = GetObservedWorld())
    {
        if (UWandererAudioSubsystem* System = World->GetSubsystem<UWandererAudioSubsystem>())
        {
            Text += TEXT("\n") + System->GetSummary();
        }
    }
    if (VisualizationController->IsEnabled())
    {
        Text += FString::Printf(
                    TEXT("\n附近声源关系：PIE独立检查角色 %.0f "
                         "米内Point/Area/Spline，不受列表筛选和选中限制，最多%d个，%s"
                         "优先。每个来源用同色点/"
                         "线及名称标明；黄点为角色。未播放只列状态，无伪造声源点；Area内部实际点可"
                         "能与听者重合。编辑态保留Spl"
                         "ine轮廓。最小化或隐藏面板仍持续绘制，取消勾选或关闭面板后停止；已Post不保"
                         "证物理可听Voice。"),
                    VisualizationController->GetRangeM(),
                    AudioVisualizationSettings::RelationCountMax,
                    AudioVisualizationSettings::bPrioritizePostedSources ? TEXT("已播放")
                                                                         : TEXT("距离"))
                + VisualizationController->GetSummary();
    }
    if (!State.LastRoomAction.IsEmpty())
    {
        Text += TEXT("\n") + State.LastRoomAction;
    }
    State.Summary = FText::FromString(Text);
}

void FAudioManagerController::AddWorldEntries(UWorld& InWorld, AWandererWorldAudioBudget* InBudget)
{
    TArray<FEntryPtr> WorldEntries;
    TMap<FSoftObjectPath, FRoomSources> RoomSources;
    for (TActorIterator<AActor> It(&InWorld); It; ++It)
    {
        CollectActorEntries(**It, WorldEntries, RoomSources);
    }

    // All associations are known before filtering, including linked emitters later in actor order.
    const FRoomSources EmptySources;
    for (const FEntryPtr& Entry : WorldEntries)
    {
        const FRoomSources* Sources = RoomSources.Find(FSoftObjectPath(Entry->ActorWeakPtr.Get()));
        FillEntry(*Entry, InBudget, Sources ? *Sources : EmptySources);
        if (MatchesWorldEntryFilter(*Entry))
        {
            State.Entries.Add(Entry);
        }
    }
}

void FAudioManagerController::CollectActorEntries(
    AActor& InActor,
    TArray<FEntryPtr>& InOutEntries,
    TMap<FSoftObjectPath, FRoomSources>& InOutRoomSources)
{
    TInlineComponentArray<UWandererAudioSourceComponent*> Sources(&InActor);
    if (Sources.IsEmpty())
    {
        const bool bAudio = InActor.ActorHasTag(TEXT("AudioAutoFitPreview"))
                            || (InActor.GetWorld()->WorldType == EWorldType::Editor
                                && FInteriorFitGeometryImportOperation::IsSource(&InActor))
                            || InActor.IsA<AAkSpatialAudioVolume>()
                            || InActor.IsA<AAkAcousticPortal>() || InActor.IsA<AAkAmbientSound>()
                            || InActor.FindComponentByClass<UAkComponent>();
        if (!bAudio)
        {
            return;
        }
        Sources.Add(nullptr);
    }
    bool bFirstRoomSource = InActor.IsA<AAkSpatialAudioVolume>();
    for (UWandererAudioSourceComponent* Source : Sources)
    {
        FEntryPtr Entry = MakeShared<FEntry>();
        Entry->ActorWeakPtr = &InActor;
        Entry->SourceWeakPtr = Source;
        // Preserve the primary Room row when its first source is added.
        Entry->bPrimaryRoomRow = bFirstRoomSource;
        bFirstRoomSource = false;
        InOutEntries.Add(Entry);
        if (Source)
        {
            CollectRoomAssociations(*Source, InOutRoomSources);
        }
    }
}

void FAudioManagerController::CollectRoomAssociations(
    UWandererAudioSourceComponent& InSource,
    TMap<FSoftObjectPath, FRoomSources>& InOutRoomSources)
{
    if (AAkSpatialAudioVolume* Room = InSource.ResolveRoom())
    {
        InOutRoomSources.FindOrAdd(FSoftObjectPath(Room)).Add(&InSource);
    }
    const UWandererBuildingAmbienceComponent* Building =
        Cast<UWandererBuildingAmbienceComponent>(&InSource);
    if (!Building)
    {
        return;
    }
    for (const TSoftObjectPtr<AActor>& Member : Building->MemberRooms)
    {
        const FSoftObjectPath Path =
            ActorPathUtils::ForWorld(Member.ToSoftObjectPath(), InSource.GetWorld());
        if (!Path.IsNull())
        {
            InOutRoomSources.FindOrAdd(Path).AddUnique(&InSource);
        }
    }
}

bool FAudioManagerController::MatchesWorldEntryFilter(const FEntry& InEntry) const
{
    const UWandererAudioSourceComponent* Source = InEntry.SourceWeakPtr.Get();
    if (State.ActivePage == 3
        && (!Source || !Source->IsA<UWandererBuildingAmbienceComponent>()))
    {
        return false;
    }
    return (State.TypeFilter == TEXT("全部类型") || State.TypeFilter == InEntry.Kind)
           && (State.GroupFilter == TEXT("全部资源组")
               || State.GroupFilter == InEntry.Cells.FindRef("Group"))
           && (State.Filter.IsEmpty() || InEntry.Description.Contains(State.Filter));
}

void FAudioManagerController::RestoreEntries(const TArray<FEntryPtr>& InPreviousEntries,
                                             const TArray<FEntryPtr>& InPreviousSelection)
{
    const auto KeyOf = [](const FEntryPtr& InEntry)
    {
        return MakeTuple(InEntry->ActorWeakPtr,
                         InEntry->bPrimaryRoomRow ? TWeakObjectPtr<UWandererAudioSourceComponent>()
                                                  : InEntry->SourceWeakPtr,
                         InEntry->ResourceGroupName,
                         InEntry->AssetPath);
    };
    using FRowKey = decltype(KeyOf(FEntryPtr()));
    TMap<FRowKey, FEntryPtr> PreviousByKey;
    TSet<FRowKey> DuplicateKeys;
    TSet<FRowKey> CurrentKeys;
    for (const FEntryPtr& Entry : State.Entries)
    {
        const FRowKey Key = KeyOf(Entry);
        if (CurrentKeys.Contains(Key))
        {
            DuplicateKeys.Add(Key);
        }
        CurrentKeys.Add(Key);
    }
    for (const FEntryPtr& Entry : InPreviousEntries)
    {
        const FRowKey Key = KeyOf(Entry);
        if (PreviousByKey.Contains(Key))
        {
            DuplicateKeys.Add(Key);
        }
        PreviousByKey.Add(Key, Entry);
    }
    for (FEntryPtr& Entry : State.Entries)
    {
        const FRowKey Key = KeyOf(Entry);
        if (DuplicateKeys.Contains(Key))
        {
            continue;
        }
        if (const FEntryPtr* Previous = PreviousByKey.Find(Key))
        {
            **Previous = MoveTemp(*Entry);
            Entry = *Previous;
        }
    }
    SortEntries();
    SelectedEntries.Reset();
    for (const FEntryPtr& Entry : InPreviousSelection)
    {
        if (State.Entries.Contains(Entry))
        {
            SelectedEntries.Add(Entry);
        }
    }
}

bool FAudioManagerController::TickManager(float InDeltaSeconds)
{
    if (bStopped)
    {
        return false;
    }
    // Keep source discovery and PIE world changes alive even when Slate hides the panel.
    if (IsGarbageCollecting())
    {
        return true;
    }
    const double Time = FPlatformTime::Seconds();
    if (bDetailsRefreshPending || ObservedWorldWeakPtr.Get() != GetObservedWorld())
    {
        bDetailsRefreshPending = false;
        Refresh();
    }
    if (bStopped || Time < StateRefreshNextAtSec)
    {
        return !bStopped;
    }
    StateRefreshNextAtSec = Time + AudioManagerRefreshSettings::StateRefreshIntervalSec;
    const uint64 Revision = FInteriorFitAudioSyncService::GetRevision();
    if (Revision != LastGeometrySyncRevision)
    {
        LastGeometrySyncRevision = Revision;
        Refresh();
    }
    // Avoid rebuilding Details during editor typing. PIE rows use live text below.
    if (GEditor && GEditor->PlayWorld && State.bProjectAssets)
    {
        VisualizationController->RefreshSources();
    }
    if (GEditor && GEditor->PlayWorld && !State.bProjectAssets)
    {
        Refresh();
    }
    else
    {
        UpdateSummary();
    }
    return true;
}

void FAudioManagerController::UpdateGroupOptions(AWandererWorldAudioBudget* InBudget)
{
    if (!IsGroupPickerOpen())
    {
        State.GroupOptions = {MakeShared<FString>(TEXT("全部资源组")),
                              MakeShared<FString>(TEXT("None"))};
        if (InBudget)
        {
            for (const FWandererAudioResourceGroup& Group : InBudget->ResourceGroups)
            {
                State.GroupOptions.Add(MakeShared<FString>(Group.Name.ToString()));
            }
        }
        RefreshGroupOptions();
        if (bStopped)
        {
            return;
        }
    }
}
