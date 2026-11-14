#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

#include "Features/AudioManager/AudioManagerTypes.h"

class IAudioManagerInteraction;
class FAudioVisualizationController;
class AWandererWorldAudioBudget;
class AAkSpatialAudioVolume;
class AWandererWorldAudioEmitter;
class UAkAudioEvent;
class UWandererBuildingAmbienceComponent;
struct FPropertyAndParent;
struct FWandererAudioResourceGroup;
enum class EGeometryImportTransform : uint8;

/** Owns table/selection and authoring requests; presentation is a weak interface. */
enum class ERoomAudioEdit : uint8;

class FAudioManagerController : public TSharedFromThis<FAudioManagerController>
{
public:
    using FEntry = FAudioManagerEntry;
    using FEntryPtr = FAudioManagerEntryPtr;
    using FColumnDefinition = FAudioManagerColumn;
    using FRoomSources = TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>;
    using FSourceDialogDraft = FAudioSourceDialogDraft;

    FAudioManagerController();
    ~FAudioManagerController();
    void Start(TSharedRef<IAudioManagerInteraction> InInteraction);
    void Stop();
    const FAudioManagerState& GetState() const;
    TSharedRef<FAudioVisualizationController> GetVisualizationController() const;
    void SetPage(int32 InPage);
    void SetFilter(const FString& InFilter);
    void SetTypeFilter(const FString& InFilter);
    void SetGroupFilter(const FString& InFilter);
    void SetProjectAssets(bool bInProjectAssets);
    void SetSort(FName InColumn, EColumnSortMode::Type InMode);
    void Refresh();
    void QueueDetailsRefresh();
    void SelectionChanged(const TArray<FEntryPtr>& InSelection);
    void OpenSelection();
    void FocusEntries(const TArray<FEntryPtr>& InSelected);
    void EditShapes();
    void EditBudget();
    UWorld* GetObservedWorld() const;
    bool CanEditDetails() const;
    bool IsPropertyReadOnly(const FPropertyAndParent& InItem) const;
    bool CanEditRoomSources() const;
    TArray<AAkSpatialAudioVolume*> GetSelectedRooms() const;
    TArray<UWandererBuildingAmbienceComponent*> GetSelectedBuildings() const;
    TArray<UWandererAudioSourceComponent*> GetSelectedRoomSources() const;
    TArray<FName> GetBuildingResourceGroups() const;
    void CreateBuildingDialog();
    void EditBuildingSelection();
    void DeleteBuildingSelection();
    void ApplyBuildingResourceGroup(FName InGroup);
    bool ApplyBuildingDraft(TSharedRef<FAudioBuildingDraft> InDraft);
    void OpenSourceDialog(EAudioSourceAction InAction);
    static FString GetSourceProfileName(const FAudioSourceDialogDraft& InDraft);
    bool ApplySourceDraft(TSharedRef<FAudioSourceDialogDraft> InDraft);
    void UpdateImportedGeometry(EGeometryImportTransform InTransformPolicy);
    void ToggleRoomActions();
    void SetRoomProfileTable(FSoftObjectPath InPath);
    void SetRoomProfileRow(FName InRow);
    void SetRoomEvent(FSoftObjectPath InPath);
    void SetRoomNightEvent(FSoftObjectPath InPath);
    void RefreshRoomProfiles();
    void SelectVisibleRooms();
    void CreateRoomSources();
    void EditRoomSources();
    void ApplyRoomProfile();
    void ApplyRoomEvent();
    void ApplyRoomSelection();
    void AssociateRoomSources();
    void DeleteRoomSources(bool bInConfirm = true);
    static TArray<FColumnDefinition> GetColumnDefinitions(int32 InPage);
    bool IsColumnVisible(int32 InPage, FName InColumn) const;
    void SetColumnVisibility(int32 InPage, FName InColumn, bool bInVisible);
    void ResetColumnVisibility(int32 InPage);
    void ShowAllColumns(int32 InPage);
    void LoadColumnVisibility(const FString& InFilename = FString());
    void SaveColumnVisibility(const FString& InFilename = FString()) const;
    void UpdateSummary();

private:
    friend class FSplineCharacterLinkTest;
    friend class FAudioHiddenVisualizationTest;
    friend class FAudioStoppedPanelTest;
    friend class FAudioControllerContractTest;
    friend class FAudioManagerTableTest;
    friend class FAudioColumnSettingsTest;
    friend class FAudioSceneLocateTest;
    friend class FAudioProfileOpenTest;
    friend class FNearbySourceRelationsProbe;
    friend class FAudioRoomActionsTest;
    friend class FAudioSavedRoomActionsTest;
    friend class FBuildingAmbienceAuthoringTest;
    friend class FRoomAdditiveAuthoringTest;

    void AddProjectAssets();
    void AddResourceEntries(AWandererWorldAudioBudget& InBudget);
    static void CollectResourceSourceCounts(UWorld& InWorld,
                                            TMap<FName, int32>& OutCounts);
    void FillResourceEntry(FEntry& InOutEntry,
                           const FWandererAudioResourceGroup& InGroup,
                           int32 InSourceCount,
                           const FString& InPolicyError) const;
    void AddWorldEntries(UWorld& InWorld, AWandererWorldAudioBudget* InBudget);
    static void CollectActorEntries(AActor& InActor,
                                    TArray<FEntryPtr>& InOutEntries,
                                    TMap<FSoftObjectPath, FRoomSources>& InOutRoomSources);
    static void CollectRoomAssociations(UWandererAudioSourceComponent& InSource,
                                        TMap<FSoftObjectPath, FRoomSources>& InOutRoomSources);
    bool MatchesWorldEntryFilter(const FEntry& InEntry) const;
    void ApplyRoomEdit(ERoomAudioEdit InAction);
    void CollectSourceTargets(FAudioSourceDialogDraft& InOutDraft);
    bool ConfirmRoomDeletion(int32 InCount);
    void DeleteRoomTargets(const TArray<TWeakObjectPtr<AWandererWorldAudioEmitter>>& InTargets,
        int32& InOutDeleted,
        int32& InOutSkipped);
    void FillBuildingCells(
        FEntry& InOutEntry, UWandererBuildingAmbienceComponent& InBuilding) const;
    void FillEntry(FEntry& InOutEntry,
        AWandererWorldAudioBudget* InBudget,
        const FRoomSources& InRoomSources) const;
    void FillRoomSources(FEntry& InOutEntry, const FRoomSources& InRoomSources) const;
    void FillSourceCells(FEntry& InOutEntry, UWandererAudioSourceComponent& InSource) const;
    void FillSpatialGroup(FEntry& InOutEntry, AWandererWorldAudioBudget* InBudget) const;
    AWandererWorldAudioBudget* FindBudget() const;
    void FinishRoomAction(const FString& InResult);
    void GatherRoomDeletionTargets(
        TArray<TWeakObjectPtr<AWandererWorldAudioEmitter>>& OutTargetsWeakPtr,
        FRoomSources& OutRoomComponentsWeakPtr,
        int32& InOutSkipped) const;
    TArray<FSoftObjectPath> GetOpenAssets(const FEntryPtr& InEntry) const;
    static void CollectRoomProfileTables(AAkSpatialAudioVolume& InRoom,
                                         TArray<FSoftObjectPath>& OutAssets);
    void CollectFocusActors(UWorld& InWorld,
                            const TArray<FEntryPtr>& InSelected,
                            TArray<TWeakObjectPtr<AActor>>& OutActors) const;
    bool IsCurrentActionWorld(const TWeakObjectPtr<UWorld>& InWorldWeakPtr) const;
    void OpenSelectedAssets(const TArray<FSoftObjectPath>& InAssets,
                            const TWeakObjectPtr<UWorld>& InWorldWeakPtr,
                            int32 InActorCount);
    int32 SelectFocusActors(const TArray<TWeakObjectPtr<AActor>>& InActors,
                           const TWeakObjectPtr<UWorld>& InWorldWeakPtr);
    void FocusEditorViewport(const TArray<TWeakObjectPtr<AActor>>& InActors,
                            const TWeakObjectPtr<UWorld>& InWorldWeakPtr);
    bool GetRoomProfile(FDataTableRowHandle& OutProfile) const;
    FString GetSharedRoomNotice() const;
    void InitializeSourceProfile(FAudioSourceDialogDraft& InOutDraft);
    bool IsGroupPickerOpen() const;
    void RebuildColumns();
    void RefreshGroupOptions();
    void RefreshRoomOptions();
    void RefreshSelectedBuildingResourceGroup();
    void RefreshSelectedRoomEvent();
    void RefreshTable();
    void RestoreEntries(
        const TArray<FEntryPtr>& InPreviousEntries, const TArray<FEntryPtr>& InPreviousSelection);
    void SetDetailsObjects(const TArray<UObject*>& InObjects);
    void SortEntries();
    bool TickManager(float InDeltaSeconds);
    void UpdateGroupOptions(AWandererWorldAudioBudget* InBudget);
    void UpdateSelectionDetails();

    FAudioManagerState State;
    TArray<FEntryPtr> SelectedEntries;
    TWeakPtr<IAudioManagerInteraction> InteractionWeakPtr;
    TSharedPtr<FAudioVisualizationController> VisualizationController;
    TWeakObjectPtr<UWorld> ObservedWorldWeakPtr;
    TArray<TWeakObjectPtr<AActor>> RoomEventTargetsWeakPtr;
    FSoftObjectPath ObservedRoomPlayEvent;
    FSoftObjectPath ObservedRoomNightEvent;
    FTSTicker::FDelegateHandle RefreshTicker;
    bool bStopped = false;
    bool bRefreshing = false;
    bool bDetailsRefreshPending = false;
    uint64 LastGeometrySyncRevision = 0;
    double StateRefreshNextAtSec = 0;
};
