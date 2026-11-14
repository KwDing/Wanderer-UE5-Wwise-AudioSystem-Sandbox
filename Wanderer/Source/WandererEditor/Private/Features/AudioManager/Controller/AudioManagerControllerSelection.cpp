#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    FBox CollectResourceFocusBounds(
        const TArray<FAudioManagerController::FEntryPtr>& InEntries, const UWorld& InWorld)
    {
        FBox Bounds(ForceInit);
        for (const FAudioManagerController::FEntryPtr& Entry : InEntries)
        {
            if (!Entry || Entry->ResourceGroupName.IsNone())
            {
                continue;
            }
            const AWandererWorldAudioBudget* Budget =
                Cast<AWandererWorldAudioBudget>(Entry->ActorWeakPtr.Get());
            if (!Budget || Budget->GetWorld() != &InWorld)
            {
                continue;
            }
            for (const FWandererAudioResourceGroup& Group : Budget->ResourceGroups)
            {
                if (Group.Name == Entry->ResourceGroupName)
                {
                    Bounds += FBox(Group.BoundsCenter - Group.BoundsExtent,
                        Group.BoundsCenter + Group.BoundsExtent);
                }
            }
        }
        return Bounds;
    }

    void AddBoundTable(const FDataTableRowHandle& InHandle, TArray<FSoftObjectPath>& OutAssets)
    {
        const UDataTable* Table = InHandle.DataTable.Get();
        if (IsValid(Table) && !InHandle.RowName.IsNone()
            && Table->GetRowMap().Contains(InHandle.RowName))
        {
            OutAssets.AddUnique(FSoftObjectPath(Table));
        }
    }

    void AddSourceProfileTable(const UWandererAudioSourceComponent* InSource,
                               TArray<FSoftObjectPath>& OutAssets)
    {
        // Buildings consume their inline playback configuration.
        if (InSource && !InSource->IsA<UWandererBuildingAmbienceComponent>()
            && InSource->GetProfile())
        {
            AddBoundTable(InSource->Profile, OutAssets);
        }
    }
} // namespace

void FAudioManagerController::OpenSelection()
{
    if (bStopped || !GEditor || GEditor->PlayWorld || IsGarbageCollecting())
    {
        return;
    }
    const TSharedRef<FAudioManagerController> KeepAlive = AsShared();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GetObservedWorld();
    const TArray<FEntryPtr> Selection = SelectedEntries;
    TArray<FSoftObjectPath> Assets;
    TArray<FEntryPtr> Actors;
    for (const FEntryPtr& Entry : Selection)
    {
        const TArray<FSoftObjectPath> Paths = GetOpenAssets(Entry);
        for (const FSoftObjectPath& Path : Paths)
        {
            Assets.AddUnique(Path);
        }
        if (Paths.IsEmpty() && Entry->ActorWeakPtr.IsValid())
        {
            Actors.Add(Entry);
        }
    }
    // Focusing uses the current world, which opening a map may replace.
    if (!Actors.IsEmpty())
    {
        FocusEntries(Actors);
    }
    if (!IsCurrentActionWorld(WorldWeakPtr) || GEditor->PlayWorld)
    {
        return;
    }
    OpenSelectedAssets(Assets, WorldWeakPtr, Actors.Num());
}

void FAudioManagerController::FocusEntries(const TArray<FEntryPtr>& InSelected)
{
    UWorld* World = GetObservedWorld();
    if (!World || IsGarbageCollecting())
    {
        return;
    }
    const TSharedRef<FAudioManagerController> KeepAlive = AsShared();
    const TWeakObjectPtr<UWorld> WorldWeakPtr = World;
    TArray<TWeakObjectPtr<AActor>> Actors;
    CollectFocusActors(*World, InSelected, Actors);
    if (Actors.IsEmpty())
    {
        FinishRoomAction(TEXT("所选条目没有可定位的当前世界对象。"));
        return;
    }

    GEditor->SelectNone(false, true);
    const int32 Selected = SelectFocusActors(Actors, WorldWeakPtr);
    if (!IsCurrentActionWorld(WorldWeakPtr))
    {
        return;
    }
    GEditor->NoteSelectionChange();
    if (!IsCurrentActionWorld(WorldWeakPtr))
    {
        return;
    }
    State.LastRoomAction = FString::Printf(TEXT("已选中 %d 个场景对象。%s"), Selected,
        GEditor->PlayWorld ? TEXT("PIE 中不移动编辑器视口；大纲需显示当前游戏世界。")
                           : TEXT("已聚焦编辑器视口。"));
    UpdateSummary();
    if (Selected > 0 && !GEditor->PlayWorld)
    {
        const FBox ResourceBounds = CollectResourceFocusBounds(InSelected, *WorldWeakPtr.Get());
        if (ResourceBounds.IsValid)
        {
            GEditor->MoveViewportCamerasToBox(ResourceBounds, true);
            return;
        }
        FocusEditorViewport(Actors, WorldWeakPtr);
    }
}

void FAudioManagerController::EditShapes()
{
    if (bStopped || !GEditor || GEditor->PlayWorld)
    {
        return;
    }
    TArray<UObject*> Objects;
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        AActor* Actor = Entry->ActorWeakPtr.Get();
        if (!Actor)
        {
            continue;
        }
        if (const auto* Audio = Entry->SourceWeakPtr.Get(); Audio && Audio->bGeometryAutoSync)
        {
            auto* Origin =
                Cast<AActor>(FSoftObjectPath(Audio->GeometryImportSource).ResolveObject());
            if (FInteriorFitGeometryImportOperation::IsSource(Origin)
                && Origin->GetWorld() == Actor->GetWorld())
            {
                Actor = Origin;
            }
        }
        Objects.AddUnique(Actor);
    }
    SetDetailsObjects(Objects);
    return;
}

void FAudioManagerController::EditBudget()
{
    if (bStopped || !GEditor || GEditor->PlayWorld)
    {
        return;
    }
    UWorld* World = GetObservedWorld();
    if (!World)
    {
        return;
    }
    AWandererWorldAudioBudget* Budget = FindBudget();
    if (!Budget)
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Create world audio budget")));
        FActorSpawnParameters Params;
        Params.OverrideLevel = World->PersistentLevel;
        Params.ObjectFlags |= RF_Transactional;
        Budget = World->SpawnActor<AWandererWorldAudioBudget>(Params);
        if (Budget)
        {
            Budget->SetActorLabel(TEXT("AUDIO_BUDGET"));
            Budget->SetFolderPath(TEXT("Audio/Management"));
        }
    }
    SetDetailsObjects({Budget});
    return;
}

TArray<FSoftObjectPath> FAudioManagerController::GetOpenAssets(const FEntryPtr& InEntry) const
{
    TArray<FSoftObjectPath> Assets;
    if (!InEntry.IsValid())
    {
        return Assets;
    }
    if (!InEntry->AssetPath.IsNull())
    {
        Assets.Add(InEntry->AssetPath);
        return Assets;
    }
    AActor* Actor = InEntry->ActorWeakPtr.Get();
    if (!Actor || Actor->GetWorld() != GetObservedWorld())
    {
        return Assets;
    }
    AddSourceProfileTable(InEntry->SourceWeakPtr.Get(), Assets);
    if (const AWandererWorldAudioBudget* Budget = Cast<AWandererWorldAudioBudget>(Actor))
    {
        const FWandererAudioResourceGroup* Match = nullptr;
        for (const FWandererAudioResourceGroup& Group : Budget->ResourceGroups)
        {
            if (Group.Name != InEntry->ResourceGroupName || Group.Name.IsNone())
            {
                continue;
            }
            if (Match)
            {
                return Assets;
            }
            Match = &Group;
        }
        if (Match)
        {
            AddBoundTable(Match->Policy, Assets);
        }
    }
    if (AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(Actor))
    {
        CollectRoomProfileTables(*Room, Assets);
    }
    return Assets;
}

void FAudioManagerController::CollectRoomProfileTables(AAkSpatialAudioVolume& InRoom,
                                                        TArray<FSoftObjectPath>& OutAssets)
{
    for (TActorIterator<AActor> It(InRoom.GetWorld()); It; ++It)
    {
        TInlineComponentArray<UWandererAudioSourceComponent*> Sources(*It);
        for (UWandererAudioSourceComponent* Source : Sources)
        {
            if (Source->ResolveRoom() == &InRoom)
            {
                AddSourceProfileTable(Source, OutAssets);
            }
        }
    }
}

void FAudioManagerController::CollectFocusActors(
    UWorld& InWorld,
    const TArray<FEntryPtr>& InSelected,
    TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
    for (const FEntryPtr& Entry : InSelected)
    {
        AActor* Actor = Entry.IsValid() ? Entry->ActorWeakPtr.Get() : nullptr;
        if (!Actor || Actor->GetWorld() != &InWorld)
        {
            continue;
        }
        const UWandererBuildingAmbienceComponent* Building =
            Cast<UWandererBuildingAmbienceComponent>(Entry->SourceWeakPtr.Get());
        if (!Building)
        {
            OutActors.AddUnique(Actor);
            continue;
        }
        for (const TSoftObjectPtr<AActor>& Member : Building->MemberRooms)
        {
            if (AActor* Room = Member.Get(); Room && Room->GetWorld() == &InWorld)
            {
                OutActors.AddUnique(Room);
            }
        }
    }
}

bool FAudioManagerController::IsCurrentActionWorld(
    const TWeakObjectPtr<UWorld>& InWorldWeakPtr) const
{
    return !bStopped && InWorldWeakPtr.IsValid() && InWorldWeakPtr.Get() == GetObservedWorld();
}

void FAudioManagerController::OpenSelectedAssets(
    const TArray<FSoftObjectPath>& InAssets,
    const TWeakObjectPtr<UWorld>& InWorldWeakPtr,
    int32 InActorCount)
{
    int32 Opened = 0;
    for (const FSoftObjectPath& Path : InAssets)
    {
        UObject* Asset = Path.TryLoad();
        if (!IsCurrentActionWorld(InWorldWeakPtr) || GEditor->PlayWorld)
        {
            return;
        }
        UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (IsValid(Asset) && Editors)
        {
            Opened += Editors->OpenEditorForAsset(Asset);
        }
        if (!IsCurrentActionWorld(InWorldWeakPtr) || GEditor->PlayWorld)
        {
            return;
        }
    }
    State.LastRoomAction = FString::Printf(TEXT("打开 %d 个资产；定位 %d 个场景条目。%s"),
        Opened, InActorCount,
        Opened < InAssets.Num() ? TEXT("部分资产打开失败，请检查资产是否仍存在。") : TEXT(""));
    UpdateSummary();
}

int32 FAudioManagerController::SelectFocusActors(
    const TArray<TWeakObjectPtr<AActor>>& InActors,
    const TWeakObjectPtr<UWorld>& InWorldWeakPtr)
{
    int32 Selected = 0;
    for (const TWeakObjectPtr<AActor>& ActorWeakPtr : InActors)
    {
        if (!IsCurrentActionWorld(InWorldWeakPtr))
        {
            break;
        }
        if (AActor* Actor = ActorWeakPtr.Get(); Actor && Actor->GetWorld() == InWorldWeakPtr.Get())
        {
            GEditor->SelectActor(Actor, true, false);
            ++Selected;
        }
    }
    return Selected;
}

void FAudioManagerController::FocusEditorViewport(
    const TArray<TWeakObjectPtr<AActor>>& InActors,
    const TWeakObjectPtr<UWorld>& InWorldWeakPtr)
{
    FBox AreaBounds(ForceInit);
    int32 AreaCount = 0;
    for (const TWeakObjectPtr<AActor>& ActorWeakPtr : InActors)
    {
        if (!IsCurrentActionWorld(InWorldWeakPtr) || GEditor->PlayWorld)
        {
            return;
        }
        if (AWandererAudioAreaEmitter* Area = Cast<AWandererAudioAreaEmitter>(ActorWeakPtr.Get());
            Area && Area->GetWorld() == InWorldWeakPtr.Get())
        {
            Area->RefreshEditorFocusBounds();
            const FVector Extent(Area->CellSize * 0.5, Area->CellSize * 0.5, 0.0);
            for (const FVector& Cell : Area->GetSurfaceCells())
            {
                AreaBounds += FBox(Cell - Extent, Cell + Extent)
                    .TransformBy(Area->GetActorTransform());
            }
            ++AreaCount;
        }
    }
    if (IsCurrentActionWorld(InWorldWeakPtr) && !GEditor->PlayWorld)
    {
        if (AreaCount == InActors.Num() && AreaBounds.IsValid)
        {
            GEditor->MoveViewportCamerasToBox(AreaBounds, true);
            return;
        }
        // Use the Level Editor's native focus policy for the active viewport.
        GEditor->Exec(InWorldWeakPtr.Get(), TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY"));
    }
}

void FAudioManagerController::UpdateSelectionDetails()
{
    if (bStopped)
    {
        return;
    }
    State.SharedRoomNotice = State.ActivePage == 0 ? GetSharedRoomNotice() : FString();
    RefreshSelectedBuildingResourceGroup();
    State.bDetailsHasBuildingSource = false;
    if (GetSelectedRooms().IsEmpty())
    {
        State.bShowRoomActions = false;
    }
    RefreshSelectedRoomEvent();
    TArray<UObject*> Objects;
    for (const auto& Item : SelectedEntries)
    {
        if (Item->SourceWeakPtr.IsValid())
        {
            Objects.AddUnique(Item->SourceWeakPtr.Get());
            State.bDetailsHasBuildingSource |=
                Item->SourceWeakPtr->IsA<UWandererBuildingAmbienceComponent>();
        }
        else if (Item->ActorWeakPtr.IsValid())
        {
            Objects.AddUnique(Item->ActorWeakPtr.Get());
        }
    }
    SetDetailsObjects(Objects);
    UpdateSummary();
}
