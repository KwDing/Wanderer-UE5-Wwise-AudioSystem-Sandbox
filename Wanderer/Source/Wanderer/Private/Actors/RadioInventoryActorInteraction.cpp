#include "Actors/RadioInventoryActor.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

#include "Actors/WandererSpeakerActor.h"
#include "Actors/WandererSpeakerPreviewActor.h"
#include "Framework/WandererPlayerController.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

bool ARadioInventoryActor::ExecuteRadioRequest(const FRadioControlRequest& InRequest)
{
    if (bShuttingDown || bPublishingIntent)
    {
        return false;
    }
    if (InRequest.Action <= ERadioControlAction::PreviewSurface)
    {
        return ExecutePlacementRequest(InRequest);
    }
    if (InRequest.Action <= ERadioControlAction::RetrieveAll)
    {
        return ExecuteSelectionRequest(InRequest);
    }
    switch (InRequest.Action)
    {
    case ERadioControlAction::OpenPanel:
        SetInteractionOpen(true);
        return IsInteractionOpen();
    case ERadioControlAction::ClosePanel:
        SetInteractionOpen(false);
        return true;
    case ERadioControlAction::CursorEntered:
        SetCursorOverPanel(true);
        return true;
    case ERadioControlAction::CursorLeft:
        SetCursorOverPanel(false);
        return true;
    default:
        return false;
    }
}

UTextureRenderTarget2D* ARadioInventoryActor::GetRadioPreview(EWandererSpeakerItem InItem) const
{
    return IsValid(PreviewStudio) ? PreviewStudio->GetPreviewTexture(InItem) : nullptr;
}

FRadioControlSnapshot ARadioInventoryActor::ReadRadioState() const
{
    FRadioControlSnapshot State;
    State.bAvailable = IsGameplayReady();
    const AWandererPlayerController* Player = GetController();
    State.bVisible = Player && Player->IsRadioHUDVisible();
    State.bOpen = bInteractionOpen;
    State.bPlacing = IsPlacing();
    State.bCanConfirm = bPreviewValid;
    State.Status = Status;
    State.SelectedContentID = GetSelectedOption();
    State.AvailableRadios = GetAvailableCount(EWandererSpeakerItem::Radio);
    State.AvailableMounted = GetAvailableCount(EWandererSpeakerItem::Mounted);
    for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
    {
        if (!SourceWeakPtr.IsValid())
        {
            continue;
        }
        FRadioDeviceSnapshot& Device = State.Devices.AddDefaulted_GetRef();
        Device.ID = SourceWeakPtr->GetDeviceID();
        Device.Number = SourceWeakPtr->GetDeviceNumber();
        Device.ContentID = SourceWeakPtr->GetOptionID();
        Device.Item = SourceWeakPtr->GetItemType();
        Device.bPlaying = SourceWeakPtr->IsPlaybackRequested();
        Device.bRetrievable = CanRetrieveSource(SourceWeakPtr.Get());
    }
    if (SelectedSourceWeakPtr.IsValid())
    {
        State.SelectedID = SelectedSourceWeakPtr->GetDeviceID();
        const AWandererSpeakerActor* Selected = SelectedSourceWeakPtr.Get();
        const UWorld* World = GetWorld();
        const auto* Audio = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;
        if (Audio && Selected->IsPlaybackRequested())
        {
            const ERadioPlaybackPhase Phase = Audio->GetRadioPlaybackPhase(
                const_cast<ARadioInventoryActor*>(this), Selected->GetOptionID());
            if (Phase == ERadioPlaybackPhase::LoadingMedia)
            {
                State.Status = TEXT("Loading radio audio.");
            }
            else if (Phase == ERadioPlaybackPhase::WaitingForEmitters)
            {
                State.Status = TEXT("Waiting for a radio emitter.");
            }
            else if (Phase == ERadioPlaybackPhase::WaitingForListener)
            {
                State.Status = TEXT("Waiting for the spatial audio listener.");
            }
        }
    }
    return State;
}

void ARadioInventoryActor::SetRadioPreviewEnabled(bool bInEnabled)
{
    bPreviewRequested = bInEnabled;
    if (!bInEnabled || bShuttingDown)
    {
        if (IsValid(PreviewStudio))
        {
            PreviewStudio->Destroy();
        }
        PreviewStudio = nullptr;
        bPreviewFailed = false;
        return;
    }
    if (IsValid(PreviewStudio) || bPreviewFailed || !IsGameplayReady())
    {
        return;
    }
    FActorSpawnParameters Parameters;
    if (!PrepareSpeakerMeshes())
    {
        return;
    }
    Parameters.Owner = this;
    Parameters.ObjectFlags |= RF_Transient;
    constexpr double StudioHeight = 1000000;
    PreviewStudio = GetWorld()->SpawnActor<AWandererSpeakerPreviewActor>(
        FVector(0, 0, StudioHeight), FRotator::ZeroRotator, Parameters);
    if (!IsValid(PreviewStudio) || !PreviewStudio->ShowItems(RadioMesh, MountedMesh))
    {
        UE_LOG(
            LogTemp, Warning, TEXT("Radio inventory previews could not load configured meshes."));
        SetRadioPreviewEnabled(false);
        bPreviewFailed = true;
    }
}

bool ARadioInventoryActor::ExecutePlacementRequest(const FRadioControlRequest& InRequest)
{
    switch (InRequest.Action)
    {
    case ERadioControlAction::BeginPlacement:
        return BeginItemPlacement(InRequest.Item);
    case ERadioControlAction::CancelPlacement:
        CancelPlacement();
        return true;
    case ERadioControlAction::ConfirmPlacement:
        return ConfirmPlacement();
    case ERadioControlAction::ConfirmFromPointer:
        return ConfirmPlacementFromPointer();
    case ERadioControlAction::PreviewSurface:
        return PreviewSurface(InRequest.SurfacePoint, InRequest.SurfaceNormal);
    case ERadioControlAction::RotatePlacement:
        RotatePreview();
        return true;
    default:
        return false;
    }
}

bool ARadioInventoryActor::ExecuteSelectionRequest(const FRadioControlRequest& InRequest)
{
    switch (InRequest.Action)
    {
    case ERadioControlAction::SelectAtCursor:
        return SelectSourceAtCursor();
    case ERadioControlAction::CancelSelection:
        CancelSelection();
        return true;
    case ERadioControlAction::SelectDevice:
        for (const TWeakObjectPtr<AWandererSpeakerActor>& SourceWeakPtr : SourcesWeakPtrs)
        {
            if (SourceWeakPtr.IsValid() && SourceWeakPtr->GetDeviceID() == InRequest.DeviceID)
            {
                return SelectSource(SourceWeakPtr.Get());
            }
        }
        return false;
    case ERadioControlAction::SelectContent:
        return SelectOption(InRequest.ContentID);
    case ERadioControlAction::TogglePlayback:
        return TogglePlaybackRequest();
    case ERadioControlAction::RetrieveSelected:
        return RetrieveSource(SelectedSourceWeakPtr.Get());
    case ERadioControlAction::RetrieveAll:
        return RetrieveAllSources();
    default:
        return false;
    }
}
