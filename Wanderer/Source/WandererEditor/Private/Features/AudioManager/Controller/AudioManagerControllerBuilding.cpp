#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"

#include "AkAudioEvent.h"
#include "AkSpatialAudioVolume.h"

#include "Features/AudioManager/AudioManagerInteraction.h"
#include "Features/AudioManager/Authoring/AudioComponentDeletionOperation.h"
#include "Features/AudioManager/Authoring/BuildingAudioEditOperation.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"

TArray<UWandererBuildingAmbienceComponent*> FAudioManagerController::GetSelectedBuildings() const
{
    TArray<UWandererBuildingAmbienceComponent*> Result;
    const auto Rooms = GetSelectedRooms();
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (auto* Building = Cast<UWandererBuildingAmbienceComponent>(Entry->SourceWeakPtr.Get()))
        {
            Result.AddUnique(Building);
        }
    }
    if (!Rooms.IsEmpty() && GetObservedWorld())
    {
        for (TActorIterator<AActor> It(GetObservedWorld()); It; ++It)
        {
            TArray<UWandererBuildingAmbienceComponent*> Buildings;
            It->GetComponents(Buildings);
            for (UWandererBuildingAmbienceComponent* Building : Buildings)
            {
                for (AAkSpatialAudioVolume* Room : Rooms)
                {
                    if (Building->ContainsRoom(Room))
                    {
                        Result.AddUnique(Building);
                    }
                }
            }
        }
    }
    return Result;
}

void FAudioManagerController::CreateBuildingDialog()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    auto Draft = MakeShared<FAudioBuildingDraft>();
    Draft->WorldWeakPtr = GetObservedWorld();
    for (AAkSpatialAudioVolume* Room : GetSelectedRooms())
    {
        Draft->RoomsWeakPtr.Add(Room);
    }
    if (Draft->RoomsWeakPtr.IsEmpty())
    {
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        {
            if (auto* Room = Cast<AAkSpatialAudioVolume>(*It);
                Room && Room->GetWorld() == Draft->WorldWeakPtr.Get())
            {
                Draft->RoomsWeakPtr.AddUnique(Room);
            }
        }
    }
    if (Draft->RoomsWeakPtr.IsEmpty())
    {
        return FinishRoomAction(
            TEXT("先在音频对象页或世界大纲中多选同一栋建筑的 Acoustic Rooms。"));
    }
    Draft->Name = TEXT("Building");
    if (AActor* Room = Draft->RoomsWeakPtr[0].Get())
    {
        Draft->Name = Room->GetActorLabel();
        Draft->Name.RemoveFromStart(TEXT("ACOUSTIC_ROOM"));
        while (Draft->Name.StartsWith(TEXT("_")))
        {
            Draft->Name.RightChopInline(1);
        }
    }
    Draft->Event = State.RoomEventPath.IsNull()
                       ? FSoftObjectPath(
                             TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"))
                       : State.RoomEventPath;
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->ShowBuildingDialog(Draft);
    }
}

void FAudioManagerController::EditBuildingSelection()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    if (State.ActivePage != 3)
    {
        return FinishRoomAction(
            TEXT("未修改：建筑共用底噪只能在“建筑底噪”页编辑；Room 入口不会打开共用记录。"));
    }
    TArray<UObject*> Objects;
    TArray<FString> Names;
    for (UWandererBuildingAmbienceComponent* Building : GetSelectedBuildings())
    {
        Objects.Add(Building);
        Names.Add(FString::Printf(TEXT("%s（%d 个成员 Room）"), *Building->BuildingName.ToString(),
                                  Building->MemberRooms.Num()));
    }
    if (Objects.IsEmpty())
    {
        return FinishRoomAction(TEXT("请选择建筑底噪记录，或其成员 Room。"));
    }
    SetDetailsObjects(Objects);
    State.LastRoomAction = FString::Printf(
        TEXT(
            "正在建筑底噪页编辑 %d 条共用记录：%s。右侧直接修改，影响该建筑全部成员 Room；成员 "
            "Room 自身的 Ak Event "
            "不会同步改变。清空 Play Event "
            "会使底噪配置缺少事件；临时停用请取消启用，移除底噪请删除建筑记录。成员建议逐栋编辑；Ct"
            "rl+Z 撤销，修改后保存地图。"),
        Objects.Num(), *FString::Join(Names, TEXT("、")));
    UpdateSummary();
    return;
}

void FAudioManagerController::DeleteBuildingSelection()
{
    if (!CanEditRoomSources())
    {
        return;
    }
    if (State.ActivePage != 3)
    {
        return FinishRoomAction(
            TEXT("未删除：请到“建筑底噪”页删除共用记录；Room 入口不会删除建筑底噪。"));
    }
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GetObservedWorld();
    TArray<TWeakObjectPtr<UWandererBuildingAmbienceComponent>> BuildingsWeakPtr;
    for (UWandererBuildingAmbienceComponent* Building : GetSelectedBuildings())
    {
        BuildingsWeakPtr.Add(Building);
    }
    if (BuildingsWeakPtr.IsEmpty())
    {
        return FinishRoomAction(TEXT("请选择建筑底噪记录，或其成员 Room。"));
    }
    SetDetailsObjects({});
    if (!CanEditRoomSources() || !WorldWeakPtr.IsValid()
        || GetObservedWorld() != WorldWeakPtr.Get())
    {
        return;
    }
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> RecordsWeakPtr;
    for (const TWeakObjectPtr<UWandererBuildingAmbienceComponent>& BuildingWeakPtr :
         BuildingsWeakPtr)
    {
        RecordsWeakPtr.Add(BuildingWeakPtr.Get());
    }
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    FString Error;
    const bool bDeleted = FAudioComponentDeletionOperation::Execute(
        RecordsWeakPtr, WorldWeakPtr.Get(), Error,
        [ControllerWeakPtr, WorldWeakPtr]()
        {
            const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->CanEditRoomSources()
                   && Controller->GetObservedWorld() == WorldWeakPtr.Get();
        });
    FinishRoomAction(
        bDeleted ? FString::Printf(TEXT("删除 %d 条建筑底噪；Rooms / Portals 保留，可撤销。"),
                                   RecordsWeakPtr.Num())
                 : Error);
}

void FAudioManagerController::ApplyBuildingResourceGroup(FName InGroup)
{
    if (!CanEditRoomSources() || State.ActivePage != 3)
    {
        return;
    }
    const TArray<UWandererBuildingAmbienceComponent*> Buildings = GetSelectedBuildings();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GetObservedWorld();
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    FString Error;
    const bool bApplied = FBuildingAudioEditOperation::ApplyResourceGroup(
        Buildings, InGroup, WorldWeakPtr.Get(), Error,
        [ControllerWeakPtr, WorldWeakPtr]()
        {
            const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->CanEditRoomSources()
                   && Controller->GetObservedWorld() == WorldWeakPtr.Get();
        });
    FinishRoomAction(
        bApplied
            ? FString::Printf(TEXT("已向 %d 条建筑底噪应用资源组 %s；Room 内播放规则保持不变。"),
                              Buildings.Num(), *InGroup.ToString())
            : Error);
}

bool FAudioManagerController::ApplyBuildingDraft(TSharedRef<FAudioBuildingDraft> InDraft)
{
    if (!CanEditRoomSources() || !InDraft->WorldWeakPtr.IsValid()
        || GetObservedWorld() != InDraft->WorldWeakPtr.Get())
    {
        InDraft->Error = FText::FromString(TEXT("管理器或编辑世界已失效。"));
        return false;
    }
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = InDraft->WorldWeakPtr;
    UAkAudioEvent* Event = Cast<UAkAudioEvent>(InDraft->Event.TryLoad());
    if (!CanEditRoomSources() || !WorldWeakPtr.IsValid()
        || GetObservedWorld() != WorldWeakPtr.Get())
    {
        InDraft->Error = FText::FromString(TEXT("加载期间编辑上下文失效。"));
        return false;
    }
    TArray<AActor*> Rooms;
    for (const TWeakObjectPtr<AActor>& Room : InDraft->RoomsWeakPtr)
    {
        if (Room.IsValid())
        {
            Rooms.Add(Room.Get());
        }
    }
    if (Rooms.Num() != InDraft->RoomsWeakPtr.Num())
    {
        InDraft->Error = FText::FromString(TEXT("成员已删除，请取消后重新选择。"));
        return false;
    }
    FString Error;
    UWandererBuildingAmbienceComponent* Building = FBuildingAudioEditOperation::Create(
        Rooms, FName(InDraft->Name), Event, Error,
        [ControllerWeakPtr, WorldWeakPtr]()
        {
            const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->CanEditRoomSources()
                   && Controller->GetObservedWorld() == WorldWeakPtr.Get();
        });
    if (!Building)
    {
        InDraft->Error
            = FText::FromString(TEXT("未创建：检查建筑名称/Event、持久地图 Audio Budget、"
                                     "Room 启用且 Auto Post 关闭、无重复建筑归属或锁定关卡。"));
        return false;
    }
    SetPage(3);
    for (const FEntryPtr& Entry : State.Entries)
    {
        if (Entry->SourceWeakPtr.Get() == Building)
        {
            SelectedEntries.AddUnique(Entry);
        }
    }
    RefreshTable();
    EditBuildingSelection();
    return true;
}

FString FAudioManagerController::GetSharedRoomNotice() const
{
    const auto Buildings = GetSelectedBuildings();
    if (Buildings.IsEmpty())
    {
        return FString();
    }
    const auto Rooms = GetSelectedRooms();
    if (Rooms.IsEmpty())
    {
        return TEXT("提示：所选建筑共用底噪在音频对象页只读；请到“建筑底噪”页修改 Event。");
    }
    TArray<FString> Names;
    int32 SharedRooms = 0;
    for (const auto* Building : Buildings)
    {
        Names.Add(Building->BuildingName.ToString());
    }
    for (const auto* Room : Rooms)
    {
        SharedRooms += Buildings.ContainsByPredicate([Room](const auto* Building)
                                                     { return Building->ContainsRoom(Room); });
    }
    return FString::Printf(
        TEXT("提示：选中的 %d 个 Room 中有 %d 个属于建筑共用底噪组：%s。Room 自身 Play Event "
             "默认为无；明确配置"
             "后与建筑底噪叠加，不覆盖建筑层。Room "
             "入口只编辑附加声，共用记录仅在“建筑底噪”页修改；原生 Ak Event 不自动作为附加"
             "声。"),
        Rooms.Num(), SharedRooms, *FString::Join(Names, TEXT("、")));
}

void FAudioManagerController::RefreshSelectedBuildingResourceGroup()
{
    if (bStopped)
    {
        return;
    }
    const auto Buildings = GetSelectedBuildings();
    if (Buildings.IsEmpty())
    {
        State.SelectedBuildingResourceGroup = FText::FromString(TEXT("未选择建筑"));
        return;
    }
    const FName Group = Buildings[0]->ResourceGroup;
    const bool bMixed = Buildings.ContainsByPredicate([Group](const auto* Building)
                                                      { return Building->ResourceGroup != Group; });
    State.SelectedBuildingResourceGroup
        = FText::FromString(bMixed ? TEXT("多个资源组") : Group.ToString());
}
