#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EngineUtils.h"

#include "AkAcousticPortal.h"
#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Core/ActorPathUtils.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

namespace
{
    FString SurfaceAssociation(const AActor* Actor)
    {
        TArray<FString> Labels;
        const FString Prefix = TEXT("AudioAutoFitInput:");
        for (FName Tag : Actor->Tags)
        {
            const FString Text = Tag.ToString();
            if (!Text.StartsWith(Prefix))
            {
                continue;
            }
            const FSoftObjectPath Path = ActorPathUtils::ForWorld(
                FSoftObjectPath(Text.Mid(Prefix.Len())), Actor->GetWorld());
            const auto* Surface = Cast<AActor>(Path.ResolveObject());
            Labels.AddUnique(IsValid(Surface) ? Surface->GetActorLabel()
                                              : TEXT("未加载 / 缺失: ") + Text.Mid(Prefix.Len()));
        }
        Labels.Sort();
        return FString::Join(Labels, TEXT(", "));
    }
} // namespace

void FAudioManagerController::AddProjectAssets()
{
    IAssetRegistry& Registry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter AssetFilter;
    AssetFilter.PackagePaths.Add(TEXT("/Game"));
    AssetFilter.bRecursivePaths = true;
    AssetFilter.ClassPaths = {UAkAudioEvent::StaticClass()->GetClassPathName(),
                              UDataTable::StaticClass()->GetClassPathName(),
                              UWorld::StaticClass()->GetClassPathName()};
    TArray<FAssetData> Assets;
    Registry.GetAssets(AssetFilter, Assets);
    for (const FAssetData& Asset : Assets)
    {
        const bool bTable = Asset.AssetClassPath == UDataTable::StaticClass()->GetClassPathName();
        if (bTable && !Asset.GetTagValueRef<FString>(TEXT("RowStructure")).Contains(TEXT("Audio")))
        {
            continue;
        }
        FEntryPtr Entry = MakeShared<FEntry>();
        Entry->AssetPath = Asset.GetSoftObjectPath();
        Entry->Kind = bTable ? TEXT("DT 资产")
                      : Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName()
                          ? TEXT("地图资产")
                          : TEXT("Event 资产");
        Entry->Description = Entry->AssetPath.ToString() + TEXT(" ") + Entry->Kind;
        Entry->Cells.Add("Name", Asset.AssetName.ToString());
        Entry->Cells.Add("Type", Entry->Kind);
        Entry->Cells.Add("Level", Asset.PackageName.ToString());
        Entry->Cells.Add("Status", TEXT("项目资产 / 未加载"));
        if ((State.TypeFilter == TEXT("全部类型") || State.TypeFilter == Entry->Kind)
            && (State.Filter.IsEmpty() || Entry->Description.Contains(State.Filter)))
        {
            State.Entries.Add(Entry);
        }
    }
}

void FAudioManagerController::AddResourceEntries(AWandererWorldAudioBudget& InBudget)
{
    TMap<FName, int32> SourceCounts;
    CollectResourceSourceCounts(*InBudget.GetWorld(), SourceCounts);
    TArray<FWandererAudioResourceGroup> Groups = InBudget.ResourceGroups;
    TArray<FString> PolicyErrors;
    TSet<FName> GroupNames;
    for (FWandererAudioResourceGroup& Group : Groups)
    {
        GroupNames.Add(Group.Name);
        Group.ApplyPolicy(PolicyErrors.AddDefaulted_GetRef());
    }
    State.ResourceConfigurationNotice = FWandererAudioResourceGroup::ValidateLayout(Groups);
    for (const TPair<FName, int32>& Pair : SourceCounts)
    {
        if (!Pair.Key.IsNone() && !GroupNames.Contains(Pair.Key))
        {
            State.ResourceConfigurationNotice += FString::Printf(
                TEXT("\n未知资源组 '%s'，%d 个声源仍引用此名称。"),
                *Pair.Key.ToString(), Pair.Value);
        }
    }
    for (int32 Index = 0; Index < Groups.Num(); ++Index)
    {
        const FWandererAudioResourceGroup& Group = Groups[Index];
        FEntryPtr Entry = MakeShared<FEntry>();
        Entry->ActorWeakPtr = &InBudget;
        Entry->ResourceGroupName = Group.Name;
        FillResourceEntry(*Entry, Group, SourceCounts.FindRef(Group.Name), PolicyErrors[Index]);
        if ((State.GroupFilter == TEXT("全部资源组") || State.GroupFilter == Entry->Group)
            && (State.Filter.IsEmpty() || Entry->Description.Contains(State.Filter)))
        {
            State.Entries.Add(Entry);
        }
    }
}

void FAudioManagerController::CollectResourceSourceCounts(UWorld& InWorld,
                                                          TMap<FName, int32>& OutCounts)
{
    for (TActorIterator<AActor> It(&InWorld); It; ++It)
    {
        TInlineComponentArray<UWandererAudioSourceComponent*> Components(*It);
        for (const UWandererAudioSourceComponent* Component : Components)
        {
            ++OutCounts.FindOrAdd(Component->ResourceGroup);
        }
    }
}

void FAudioManagerController::FillResourceEntry(FEntry& InOutEntry,
                                                const FWandererAudioResourceGroup& InGroup,
                                                int32 InSourceCount,
                                                const FString& InPolicyError) const
{
    InOutEntry.Group = InGroup.Name.ToString();
    InOutEntry.Cells = {{"Name", InOutEntry.Group},
                       {"Table", GetNameSafe(InGroup.Policy.DataTable)},
                       {"Row", InGroup.Policy.RowName.ToString()},
                       {"Resident", InGroup.bAlwaysResident ? TEXT("是") : TEXT("否")},
                       {"Load", FString::SanitizeFloat(InGroup.LoadDistanceM)},
                       {"Unload", FString::SanitizeFloat(InGroup.UnloadDistanceM)},
                       {"Delay", FString::SanitizeFloat(InGroup.UnloadDelaySec)},
                       {"Limit", InGroup.MaxEvents > 0 ? FString::FromInt(InGroup.MaxEvents)
                                                       : TEXT("不限（仍受地图预算）")},
                       {"Rooms", FString::FromInt(InGroup.Rooms.Num())},
                       {"Portals", FString::FromInt(InGroup.Portals.Num())},
                       {"Preload", FString::FromInt(InGroup.PreloadEvents.Num())},
                       {"Sources", FString::FromInt(InSourceCount)}};
    FString Dependencies;
    for (FName Name : InGroup.Dependencies)
    {
        Dependencies += (Dependencies.IsEmpty() ? TEXT("") : TEXT(", ")) + Name.ToString();
    }
    InOutEntry.Cells.Add("Dependencies", Dependencies);
    InOutEntry.Cells.Add("Status", InPolicyError.IsEmpty()
        ? TEXT("编辑配置；下次 Play 生效") : InPolicyError);
    if (const UWandererAudioSubsystem* System =
            InOutEntry.ActorWeakPtr->GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
    {
        InOutEntry.Cells.Add("Status", System->GetResourceGroupStatus(InGroup.Name));
        InOutEntry.Cells.Add("Admitted",
            FString::FromInt(System->GetResourceGroupAdmitted(InGroup.Name)));
        InOutEntry.Cells.Add("Limit", FString::FromInt(System->GetResourceGroupLimit(InGroup.Name)));
    }
    for (const TPair<FName, FString>& Cell : InOutEntry.Cells)
    {
        InOutEntry.Description += TEXT(" ") + Cell.Value;
    }
}

void FAudioManagerController::FillBuildingCells(
    FEntry& InOutEntry, UWandererBuildingAmbienceComponent& InBuilding) const
{
    InOutEntry.Kind = TEXT("建筑底噪");
    InOutEntry.Cells.Add("Name", InBuilding.BuildingName.ToString());
    if (!InBuilding.bBuildingEnabled)
    {
        InOutEntry.Cells.Add("Status", TEXT("已禁用"));
    }
    TArray<FString> Members;
    for (const TSoftObjectPtr<AActor>& Member : InBuilding.MemberRooms)
    {
        Members.Add(Member.Get() ? Member.Get()->GetActorLabel()
                                 : Member.ToSoftObjectPath().GetAssetName());
    }
    InOutEntry.Cells.Add("Room", FString::Join(Members, TEXT(", ")));
    InOutEntry.Cells.Add("Table", TEXT("内联配置（无独立资产）"));
    InOutEntry.Cells.Add("Row", FString::FromInt(Members.Num()) + TEXT(" 个 Room"));
    InOutEntry.Description += TEXT(" ") + InBuilding.BuildingName.ToString();
}

void FAudioManagerController::FillEntry(FEntry& InOutEntry,
                                        AWandererWorldAudioBudget* InBudget,
                                        const FRoomSources& InRoomSources) const
{
    AActor* Actor = InOutEntry.ActorWeakPtr.Get();
    UWandererAudioSourceComponent* Source = InOutEntry.SourceWeakPtr.Get();
    if (!Actor)
    {
        return;
    }
    InOutEntry.Cells.Add("Name", Actor->GetActorLabel());
    InOutEntry.Cells.Add("Level", Actor->GetLevel()->GetOutermost()->GetName());
    InOutEntry.Description = Actor->GetClass()->GetName() + TEXT(" ") + Actor->GetPathName();
    if (Source)
    {
        FillSourceCells(InOutEntry, *Source);
    }
    else
    {
        InOutEntry.Kind = Actor->IsA<AAkSpatialAudioVolume>() ? TEXT("Room 空间")
                          : Actor->IsA<AAkAcousticPortal>()   ? TEXT("Portal 门洞")
                                                              : TEXT("原生 / 设备");
        InOutEntry.Cells.Add("Status",
                             InOutEntry.Kind == TEXT("原生 / 设备") ? TEXT("独立播放系统")
                                                                    : TEXT("空间传播对象"));
        FillSpatialGroup(InOutEntry, InBudget);
        if (FInteriorFitGeometryImportOperation::IsSource(Actor))
        {
            InOutEntry.Kind = TEXT("IF 来源几何");
            InOutEntry.Cells.Add("Status", TEXT("几何来源；从新建入口配置声音"));
        }
    }
    if (const AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(Actor))
    {
        FillRoomSources(InOutEntry, InRoomSources);
        InOutEntry.Kind = TEXT("Room 空间");
        if (Room->Room && Room->Room->AkAudioEvent)
        {
            FString& Events = InOutEntry.Cells.FindOrAdd("Event");
            if (!Events.IsEmpty())
            {
                Events += TEXT("\n");
            }
            Events += FString::Printf(
                TEXT("[原生 Room%s] %s"),
                Room->Room->AutoPost ? TEXT("自动播放") : TEXT("元数据"),
                *Room->Room->AkAudioEvent->GetName());
        }
    }
    if (auto* Building = Cast<UWandererBuildingAmbienceComponent>(Source))
    {
        FillBuildingCells(InOutEntry, *Building);
    }
    const FString Surface = SurfaceAssociation(Actor);
    if (!Surface.IsEmpty())
    {
        const FString Existing = InOutEntry.Cells.FindRef("Room");
        InOutEntry.Cells.Add("Room",
                             Existing.IsEmpty() ? Surface : Existing + TEXT("; ") + Surface);
    }
    if (Actor->ActorHasTag(TEXT("AudioAutoFitPreview")))
    {
        InOutEntry.Kind = TEXT("地表预览");
        InOutEntry.Cells.Add("Status", TEXT("制作预览 · 仅编辑器 / 不播放"));
    }
    const FName SourceGroup = Source ? Source->ResourceGroup : NAME_None;
    if (Source && SourceGroup.IsNone())
    {
        InOutEntry.Cells.FindOrAdd("Status") += TEXT(" · 独立调度（不走资源组预载）");
    }
    if (!SourceGroup.IsNone() && Actor->GetWorld()->WorldType == EWorldType::Editor)
    {
        const bool bKnown = InBudget && InBudget->ResourceGroups.ContainsByPredicate(
            [SourceGroup](const FWandererAudioResourceGroup& InGroup)
            {
                return InGroup.Name == SourceGroup;
            });
        if (!bKnown)
        {
            InOutEntry.Cells.FindOrAdd("Status") += FString::Printf(
                TEXT(" · 未知资源组 '%s'"), *SourceGroup.ToString());
        }
    }
    if (!SourceGroup.IsNone())
    {
        const UWandererAudioSubsystem* System =
            Actor->GetWorld()->GetSubsystem<UWandererAudioSubsystem>();
        if (System && !System->IsResourceGroupActive(SourceGroup))
        {
            const FString Reason = System->GetResourceGroupStatus(SourceGroup);
            FString& Status = InOutEntry.Cells.FindOrAdd("Status");
            if (!Status.Contains(Reason))
            {
                Status += TEXT(" · ") + Reason;
            }
        }
    }
    InOutEntry.Cells.Add("Type", InOutEntry.Kind);
    InOutEntry.Cells.Add("Group", InOutEntry.Group.IsEmpty() ? TEXT("None") : InOutEntry.Group);
    for (const auto& Cell : InOutEntry.Cells)
    {
        InOutEntry.Description += TEXT(" ") + Cell.Value;
    }
}

void FAudioManagerController::FillRoomSources(FEntry& InOutEntry,
                                              const FRoomSources& InRoomSources) const
{
    TMap<FName, TArray<FString>> Values;
    int32 Count = 0;
    int32 Disabled = 0;
    int32 Invalid = 0;
    TArray<FString> Buildings;
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& WeakSource : InRoomSources)
    {
        UWandererAudioSourceComponent* Source = WeakSource.Get();
        if (!Source)
        {
            continue;
        }
        const auto* Building = Cast<UWandererBuildingAmbienceComponent>(Source);
        if (Building)
        {
            Buildings.AddUnique(Building->BuildingName.ToString());
            Values.FindOrAdd("Room").AddUnique(Building->BuildingName.ToString());
            const FWandererWorldAudioProfileRow* Profile = Building->GetProfile();
            const FString Event = Building->GetActiveEventName().IsEmpty()
                                      ? Profile->PlayEvent.ToSoftObjectPath().GetAssetName()
                                      : Building->GetActiveEventName();
            if (!Event.IsEmpty())
            {
                Values.FindOrAdd("BuildingEvent").AddUnique(Event);
            }
            continue;
        }
        ++Count;
        Disabled += !Source->bEnabled;
        Invalid += !Source->ValidateConfiguration().IsEmpty();
        Values.FindOrAdd("Room").AddUnique(Source->GetOwner()->GetActorLabel());
        Values.FindOrAdd("Table").AddUnique(GetNameSafe(Source->Profile.DataTable));
        Values.FindOrAdd("Row").AddUnique(Source->Profile.RowName.ToString());
        const FWandererWorldAudioProfileRow* Row = Source->GetProfile();
        FString Event = Source->GetActiveEventName();
        if (Event.IsEmpty() && Row)
        {
            Event = Row->PlayEvent.ToSoftObjectPath().GetAssetName();
        }
        if (!Event.IsEmpty())
        {
            Values.FindOrAdd("Event").AddUnique(Event);
        }
    }
    for (const auto& Pair : Values)
    {
        if (Pair.Key != "Event" && Pair.Key != "BuildingEvent")
        {
            InOutEntry.Cells.Add(Pair.Key, FString::Join(Pair.Value, TEXT(", ")));
        }
    }
    TArray<FString> Events;
    for (const auto& Event : Values.FindOrAdd("BuildingEvent"))
    {
        Events.Add(TEXT("[建筑] ") + Event);
    }
    for (const auto& Event : Values.FindOrAdd("Event"))
    {
        Events.Add((Buildings.IsEmpty() ? FString() : TEXT("[自身] ")) + Event);
    }
    InOutEntry.Cells.Add("Event", FString::Join(Events, TEXT("\n")));
    InOutEntry.Cells.Add(
        "Status",
        Count == 0 ? TEXT("未配置自身声源（Room 可仅用于传播）")
                   : FString::Printf(
                         TEXT("已关联 %d 个声源；禁用 %d；配置异常 %d"), Count, Disabled, Invalid));
    if (!Buildings.IsEmpty())
    {
        InOutEntry.Cells.FindChecked("Status") +=
            TEXT("；共用建筑底噪：") + FString::Join(Buildings, TEXT("、"))
            + (Count == 0 ? TEXT("；无 Room 附加声") : TEXT("；Room 声与建筑层叠加"));
        InOutEntry.Description += TEXT(" Play Event 中[建筑]表示共用来源，[自身]表示 Room "
                                       "附加声；两者仅合并显示，不互相继承或同步。Room 编辑不"
                                       "会修改共用记录。");
    }
}

void FAudioManagerController::FillSourceCells(FEntry& InOutEntry,
                                              UWandererAudioSourceComponent& InSource) const
{
    const FWandererWorldAudioProfileRow* Row = InSource.GetProfile();
    static const TCHAR* Modes[] = {TEXT("Point 点声源"),
                                   TEXT("Room 底声"),
                                   TEXT("Global 全局"),
                                   TEXT("Area 面声源"),
                                   TEXT("Spline 样条")};
    InOutEntry.Kind = Row && static_cast<uint8>(Row->Mode) < UE_ARRAY_COUNT(Modes)
                          ? Modes[static_cast<int32>(Row->Mode)]
                          : TEXT("未配置声源");
    if (!Row && !InSource.GeometryImportSource.IsEmpty())
    {
        InOutEntry.Kind = InSource.GetOwner()->IsA<AWandererAudioAreaEmitter>()
                              ? TEXT("Area 面声源")
                              : TEXT("Spline 样条");
    }
    if (Row)
    {
        InOutEntry.Cells.Add("Category",
                             StaticEnum<EWandererWorldAudioCategory>()->GetNameStringByValue(
                                 static_cast<int64>(Row->Category)));
    }
    InOutEntry.Group = InSource.ResourceGroup.ToString();
    InOutEntry.Cells.Add("Area", InSource.Area.ToString());
    InOutEntry.Cells.Add("Table", GetNameSafe(InSource.Profile.DataTable));
    InOutEntry.Cells.Add("Row", InSource.Profile.RowName.ToString());
    InOutEntry.Cells.Add("Event",
                         InSource.GetActiveEventName().IsEmpty() && Row
                             ? Row->PlayEvent.ToSoftObjectPath().GetAssetName()
                             : InSource.GetActiveEventName());
    InOutEntry.Cells.Add("Room",
                         InSource.ResolveRoom()
                             ? InSource.ResolveRoom()->GetActorLabel()
                             : InSource.AcousticRoom.ToSoftObjectPath().GetAssetName());
    const FString Error = InSource.ValidateConfiguration();
    const bool bShowGeometryStatus = InSource.GetWorld()
                                     && InSource.GetWorld()->WorldType == EWorldType::Editor
                                     && !InSource.GeometrySyncStatus.IsEmpty();
    FString Status = bShowGeometryStatus ? InSource.GeometrySyncStatus
                     : Error.IsEmpty()   ? InSource.GetStatus()
                                         : Error;
    if (Row && !Error.IsEmpty() && bShowGeometryStatus)
    {
        Status += TEXT(" · ") + Error;
    }
    InOutEntry.Cells.Add("Status", Status);
    if (!Row && !InSource.GeometryImportSource.IsEmpty())
    {
        InOutEntry.Cells.Add("Event", TEXT("暂无 Event"));
    }
    InOutEntry.Cells.Add("Distance", FString::Printf(TEXT("%.1f"), InSource.GetDistanceM()));
    InOutEntry.Cells.Add("Priority",
                         FString::Printf(TEXT("%.0f"), InSource.GetEffectivePriority()));
    InOutEntry.Cells.Add("Playing", FString::FromInt(InSource.GetPlayingID()));
}

void FAudioManagerController::FillSpatialGroup(FEntry& InOutEntry,
                                               AWandererWorldAudioBudget* InBudget) const
{
    if (!InBudget)
    {
        return;
    }
    const AActor* Actor = InOutEntry.ActorWeakPtr.Get();
    for (const FWandererAudioResourceGroup& Group : InBudget->ResourceGroups)
    {
        const auto Matches = [Actor](const TSoftObjectPtr<AActor>& InMember)
        {
            return ActorPathUtils::ForWorld(InMember.ToSoftObjectPath(), Actor->GetWorld())
                   == FSoftObjectPath(Actor);
        };
        if (Group.Rooms.ContainsByPredicate(Matches) || Group.Portals.ContainsByPredicate(Matches))
        {
            InOutEntry.Group = Group.Name.ToString();
        }
    }
}

void FAudioManagerController::SortEntries()
{
    const bool bNumeric = State.SortColumn == "Distance" || State.SortColumn == "Priority"
                          || State.SortColumn == "Playing" || State.SortColumn == "Load"
                          || State.SortColumn == "Unload" || State.SortColumn == "Delay"
                          || State.SortColumn == "Limit" || State.SortColumn == "Admitted"
                          || State.SortColumn == "Sources" || State.SortColumn == "Rooms"
                          || State.SortColumn == "Portals" || State.SortColumn == "Preload";
    State.Entries.StableSort(
        [this, bNumeric](const FEntryPtr& A, const FEntryPtr& B)
        {
            const FString Left = A->Cells.FindRef(State.SortColumn),
                          Right = B->Cells.FindRef(State.SortColumn);
            int32 Compare = Left.Compare(Right);
            if (bNumeric)
            {
                if (Left.IsNumeric() != Right.IsNumeric())
                {
                    Compare = Left.IsNumeric() ? -1 : 1;
                }
                else if (Left.IsNumeric())
                {
                    const double X = FCString::Atod(*Left), Y = FCString::Atod(*Right);
                    Compare = X < Y ? -1 : X > Y ? 1 : 0;
                }
            }
            return State.SortMode == EColumnSortMode::Descending ? Compare > 0 : Compare < 0;
        });
}
