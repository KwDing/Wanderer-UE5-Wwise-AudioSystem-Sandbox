#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/AudioManagerData.h"

#include "Editor.h"
#include "PropertyEditorDelegates.h"
#include "UObject/UnrealType.h"

#include "Features/AudioManager/AudioManagerInteraction.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"

FAudioManagerController::FAudioManagerController()
    : VisualizationController(MakeShared<FAudioVisualizationController>())
{
}

FAudioManagerController::~FAudioManagerController()
{
    Stop();
}

void FAudioManagerController::Start(TSharedRef<IAudioManagerInteraction> InInteraction)
{
    if (bStopped || RefreshTicker.IsValid())
    {
        return;
    }
    InteractionWeakPtr = InInteraction;
    VisualizationController->Start();
    LoadColumnVisibility();
    Refresh();
    if (bStopped)
    {
        return;
    }
    RefreshTicker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(this, &FAudioManagerController::TickManager),
        AudioManagerRefreshSettings::TickIntervalSec);
}

void FAudioManagerController::Stop()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    bDetailsRefreshPending = false;
    FTSTicker::GetCoreTicker().RemoveTicker(RefreshTicker);
    RefreshTicker.Reset();
    VisualizationController->Stop();
    SetDetailsObjects({});
    SelectedEntries.Reset();
    State.Entries.Reset();
    ObservedWorldWeakPtr.Reset();
    RoomEventTargetsWeakPtr.Reset();
    InteractionWeakPtr.Reset();
}

const FAudioManagerState& FAudioManagerController::GetState() const
{
    return State;
}

TSharedRef<FAudioVisualizationController>
FAudioManagerController::GetVisualizationController() const
{
    return VisualizationController.ToSharedRef();
}

void FAudioManagerController::SetFilter(const FString& InFilter)
{
    if (bStopped)
    {
        return;
    }
    State.Filter = InFilter;
    Refresh();
}

void FAudioManagerController::SetTypeFilter(const FString& InFilter)
{
    if (bStopped)
    {
        return;
    }
    State.TypeFilter = InFilter;
    Refresh();
}

void FAudioManagerController::SetGroupFilter(const FString& InFilter)
{
    if (bStopped)
    {
        return;
    }
    State.GroupFilter = InFilter;
    Refresh();
}

void FAudioManagerController::SetProjectAssets(bool bInProjectAssets)
{
    if (bStopped)
    {
        return;
    }
    State.bProjectAssets = bInProjectAssets;
    State.TypeFilter = TEXT("全部类型");
    State.GroupFilter = TEXT("全部资源组");
    SetPage(0);
}

void FAudioManagerController::SetSort(FName InColumn, EColumnSortMode::Type InMode)
{
    if (bStopped)
    {
        return;
    }
    State.SortColumn = InColumn;
    State.SortMode = InMode;
    SortEntries();
    RefreshTable();
}

void FAudioManagerController::QueueDetailsRefresh()
{
    if (CanEditDetails())
    {
        bDetailsRefreshPending = true;
    }
}

void FAudioManagerController::SelectionChanged(const TArray<FEntryPtr>& InSelection)
{
    if (bStopped || bRefreshing)
    {
        return;
    }
    SelectedEntries.Reset();
    for (const FEntryPtr& Entry : InSelection)
    {
        if (State.Entries.Contains(Entry))
        {
            SelectedEntries.AddUnique(Entry);
        }
    }
    UpdateSelectionDetails();
}

bool FAudioManagerController::CanEditDetails() const
{
    return !bStopped && GEditor && !GEditor->PlayWorld
           && (State.ActivePage != 0 || !State.bDetailsHasBuildingSource);
}

bool FAudioManagerController::IsPropertyReadOnly(const FPropertyAndParent& InItem) const
{
    const FName Name = InItem.Property.GetFName();
    if (State.ActivePage != 1
        || !(Name == "bAlwaysResident" || Name == "LoadDistanceM" || Name == "UnloadDistanceM"
             || Name == "UnloadDelaySec" || Name == "MaxEvents"))
    {
        return false;
    }
    const AWandererWorldAudioBudget* Budget = FindBudget();
    if (!Budget)
    {
        return true;
    }
    for (int32 Index = 0; Index < InItem.ParentProperties.Num(); ++Index)
    {
        if (InItem.ParentProperties[Index]->GetFName() != TEXT("ResourceGroups")
            || !InItem.ParentArrayIndices.IsValidIndex(Index))
        {
            continue;
        }
        const int32 GroupIndex = InItem.ParentArrayIndices[Index];
        if (!Budget->ResourceGroups.IsValidIndex(GroupIndex))
        {
            continue;
        }
        const FDataTableRowHandle& Policy = Budget->ResourceGroups[GroupIndex].Policy;
        return Policy.DataTable || !Policy.RowName.IsNone();
    }
    return false;
}

TArray<FName> FAudioManagerController::GetBuildingResourceGroups() const
{
    TArray<FName> Groups = {NAME_None};
    if (const AWandererWorldAudioBudget* Budget = FindBudget())
    {
        for (const FWandererAudioResourceGroup& Group : Budget->ResourceGroups)
        {
            if (!Group.Name.IsNone())
            {
                Groups.AddUnique(Group.Name);
            }
        }
    }
    Groups.Sort(
        [](FName InA, FName InB)
        {
            return InA.ToString() < InB.ToString();
        });
    return Groups;
}

void FAudioManagerController::ToggleRoomActions()
{
    if (!bStopped)
    {
        State.bShowRoomActions = !State.bShowRoomActions;
    }
}

void FAudioManagerController::SetRoomProfileTable(FSoftObjectPath InPath)
{
    if (bStopped)
    {
        return;
    }
    State.RoomProfileTablePath = InPath;
    RefreshRoomProfiles();
}

void FAudioManagerController::SetRoomProfileRow(FName InRow)
{
    if (bStopped)
    {
        return;
    }
    State.RoomProfileRow = InRow;
}

void FAudioManagerController::SetRoomEvent(FSoftObjectPath InPath)
{
    if (bStopped)
    {
        return;
    }
    State.RoomEventPath = InPath;
}

void FAudioManagerController::SetRoomNightEvent(FSoftObjectPath InPath)
{
    if (bStopped)
    {
        return;
    }
    State.RoomNightEventPath = InPath;
}
