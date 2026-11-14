#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Features/AudioManager/Controller/AudioManagerController.h"

FReply SAudioManagerPanel::OnApplyRoomProfile()
{
    Controller->ApplyRoomProfile();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnApplyRoomSelection()
{
    Controller->ApplyRoomSelection();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnAssociateRoomSources()
{
    Controller->AssociateRoomSources();
    return FReply::Handled();
}

bool SAudioManagerPanel::OnCanEditRoomSources() const
{
    return Controller->CanEditRoomSources();
}

FReply SAudioManagerPanel::OnCreateBuildingDialog()
{
    Controller->CreateBuildingDialog();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnCreateRoomSources()
{
    Controller->CreateRoomSources();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnDeleteBuildingSelection()
{
    Controller->DeleteBuildingSelection();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnDeleteRoomSources(bool bInConfirm)
{
    Controller->DeleteRoomSources(bInConfirm);
    return FReply::Handled();
}

void SAudioManagerPanel::OnDetailsChanged(const FPropertyChangedEvent& InEvent)
{
    if (!bStopped && Controller)
    {
        Controller->QueueDetailsRefresh();
    }
}

FReply SAudioManagerPanel::OnEditBuildingSelection()
{
    Controller->EditBuildingSelection();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnEditRoomSources()
{
    Controller->EditRoomSources();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnEditShapes()
{
    Controller->EditShapes();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnFocusEntries(const TArray<FEntryPtr>& InSelected)
{
    Controller->FocusEntries(InSelected);
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnOpenSelection()
{
    Controller->OpenSelection();
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnOpenSourceDialog(EAudioSourceAction InAction)
{
    Controller->OpenSourceDialog(InAction);
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnRefresh()
{
    Controller->Refresh();
    return FReply::Handled();
}

void SAudioManagerPanel::OnRefreshRoomProfiles()
{
    Controller->RefreshRoomProfiles();
}

FReply SAudioManagerPanel::OnSelectVisibleRooms()
{
    Controller->SelectVisibleRooms();
    return FReply::Handled();
}

void SAudioManagerPanel::OnSelectionChanged(FEntryPtr InEntry, ESelectInfo::Type InSelectInfo)
{
    Controller->SelectionChanged(List->GetSelectedItems());
}

FReply SAudioManagerPanel::OnSetPage(int32 InPage)
{
    Controller->SetPage(InPage);
    return FReply::Handled();
}

FReply SAudioManagerPanel::OnUpdateImportedGeometry(EGeometryImportTransform InTransformPolicy)
{
    Controller->UpdateImportedGeometry(InTransformPolicy);
    return FReply::Handled();
}
