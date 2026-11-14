#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#include "Features/AudioManager/AudioManagerInteraction.h"

class IDetailsView;
struct FPropertyChangedEvent;
class SWindow;
class FAudioManagerController;
class SHeaderRow;
enum class EAudioSourceAction : uint8;
enum class EGeometryImportTransform : uint8;
template <typename OptionType> class SComboBox;

/** Observes loaded worlds only. Details owns no actor lifetime; rows are weak. */
class SAudioManagerPanel : public SCompoundWidget, public IAudioManagerInteraction
{
public:
    SLATE_BEGIN_ARGS(SAudioManagerPanel)
    {
    }
    SLATE_END_ARGS()

    void Construct(const FArguments& InArguments);
    virtual ~SAudioManagerPanel() override;

    void Stop();

private:
    friend class FSplineCharacterLinkTest;
    friend class FAudioHiddenVisualizationTest;
    friend class FAudioStoppedPanelTest;
    using FColumnDefinition = FAudioManagerColumn;
    using FEntry = FAudioManagerEntry;
    using FEntryPtr = FAudioManagerEntryPtr;
    class SEntryRow;
    friend class FAudioManagerTableTest;
    friend class FAudioColumnSettingsTest;
    friend class FAudioSceneLocateTest;
    friend class FAudioProfileOpenTest;
    friend class FNearbySourceRelationsProbe;
    friend class FAudioRoomActionsTest;
    friend class FAudioSavedRoomActionsTest;

    friend class FBuildingAmbienceAuthoringTest;
    friend class FRoomAdditiveAuthoringTest;

    using FSourceDialogDraft = FAudioSourceDialogDraft;

    virtual void RefreshTable(const TArray<FEntryPtr>& InSelection) override;
    virtual void SetDetailsObjects(const TArray<UObject*>& InObjects) override;
    virtual void RebuildColumns() override;
    virtual bool IsGroupPickerOpen() const override;
    virtual void RefreshGroupOptions() override;
    virtual void RefreshRoomOptions() override;
    virtual void ShowSourceDialog(TSharedRef<FSourceDialogDraft> InDraft) override;
    virtual void ShowBuildingDialog(TSharedRef<FAudioBuildingDraft> InDraft) override;
    virtual bool ConfirmRoomDeletion(int32 InCount) override;
    TSharedRef<SWidget> BuildBuildingDialogActions(
        TSharedRef<FAudioBuildingDraft> InDraft, TSharedRef<SWindow> InWindow);
    TSharedRef<SWidget> BuildBuildingDialogContents(
        TSharedRef<FAudioBuildingDraft> InDraft, TSharedRef<SWindow> InWindow);
    TSharedRef<SWidget> BuildBuildingResourceMenu();
    TSharedRef<SWidget> BuildBuildingToolbar();
    TSharedRef<SWidget> BuildColumnActions(int32 InPage);
    TSharedRef<SWidget> BuildColumnChecks(int32 InPage);
    TSharedRef<SWidget> BuildColumnSettings();
    TSharedRef<SWidget> BuildContents();
    TSharedRef<SWidget> BuildVisualizationControls();
    TSharedRef<SWidget> BuildVisualizationRange();
    TSharedRef<SWidget> BuildVisualizationToggle();
    TSharedRef<SWidget> BuildFilters();
    TSharedRef<SWidget> BuildGroupFilter();
    TSharedRef<SWidget> BuildHeading();
    TSharedRef<SWidget> BuildPageTabs();
    TSharedRef<SWidget> BuildRoomEventControls();
    TSharedRef<SWidget> BuildRoomProfileControls();
    TSharedRef<SWidget> BuildRoomProfilePicker();
    TSharedRef<SWidget> BuildRoomToolbar();
    TSharedRef<SWidget> BuildSourceActions();
    TSharedRef<SWidget> BuildSourceProfileName(TSharedRef<FSourceDialogDraft> InDraft);
    TSharedRef<SWidget> BuildSourceDialogActions(
        TSharedRef<FSourceDialogDraft> InDraft, TSharedRef<SWindow> InWindow);
    TSharedRef<SWidget> BuildSourceDialogContents(TSharedRef<FSourceDialogDraft> InDraft,
        TSharedRef<SWidget> InDetails,
        TSharedRef<SWindow> InWindow);
    TSharedRef<SWidget> BuildSpatialActions();
    TSharedRef<SWidget> BuildTable();
    TSharedRef<SWidget> BuildToolbar();
    TSharedRef<SWidget> BuildTypeFilter();
    void ConfigureDetails();
    TSharedRef<ITableRow> GenerateRow(FEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwner);
    FText GetSourceExplanation(TSharedRef<FSourceDialogDraft> InDraft) const;
    FReply OnApplyRoomProfile();
    FReply OnApplyRoomSelection();
    FReply OnAssociateRoomSources();
    bool OnCanEditRoomSources() const;
    FReply OnCreateBuildingDialog();
    FReply OnCreateRoomSources();
    FReply OnDeleteBuildingSelection();
    FReply OnDeleteRoomSources(bool bInConfirm = true);
    void OnDetailsChanged(const FPropertyChangedEvent& InEvent);
    FReply OnEditBuildingSelection();
    FReply OnEditRoomSources();
    FReply OnEditShapes();
    FReply OnFocusEntries(const TArray<FEntryPtr>& InSelected);
    FReply OnOpenSelection();
    FReply OnOpenSourceDialog(EAudioSourceAction InAction);
    FReply OnRefresh();
    void OnRefreshRoomProfiles();
    FReply OnSelectVisibleRooms();
    void OnSelectionChanged(FEntryPtr InEntry, ESelectInfo::Type InSelectInfo);
    FReply OnSetPage(int32 InPage);
    FReply OnUpdateImportedGeometry(EGeometryImportTransform InTransformPolicy);

    TSharedPtr<FAudioManagerController> Controller;

    TSharedPtr<SListView<FEntryPtr>> List;
    TSharedPtr<IDetailsView> Details;
    FDelegateHandle DetailsChangedHandle;
    TSharedPtr<SHeaderRow> Header;
    TArray<TSharedPtr<FString>> TypeOptions;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> GroupPicker;
    bool bStopped = false;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> RoomProfilePicker;
};
