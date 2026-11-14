#include "UI/Widgets/WandererSpeakerWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

#include "Data/WandererSpeakerSettings.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererPlayerController.h"
#include "UI/WandererUIAudio.h"

FReply UWandererSpeakerWidget::OnCancelPlacement()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Cancel);
    Execute(ERadioControlAction::CancelPlacement);
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnCancelSelection()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Cancel);
    Execute(ERadioControlAction::CancelSelection);
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnConfirmPlacement()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    Execute(ERadioControlAction::ConfirmPlacement);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnDeployItem(EWandererSpeakerItem InItem)
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::BeginPlacement;
    Request.Item = InItem;
    if (URadioControlModel* Model = GetModel())
    {
        Model->Execute(Request);
    }
    RefreshState();
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnRetrieveAll()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    Execute(ERadioControlAction::RetrieveAll);
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnRetrieveSelected()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    Execute(ERadioControlAction::RetrieveSelected);
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnRotatePlacement()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    Execute(ERadioControlAction::RotatePlacement);
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnSelectDevice(FGuid InID)
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::SelectDevice;
    Request.DeviceID = InID;
    if (URadioControlModel* Model = GetModel())
    {
        Model->Execute(Request);
    }
    RefreshState();
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnSelectOption(FName InID)
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::SelectContent;
    Request.ContentID = InID;
    if (URadioControlModel* Model = GetModel())
    {
        Model->Execute(Request);
    }
    RefreshState();
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnTogglePanel()
{
    SetPanelOpen(!State.bOpen);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererSpeakerWidget::OnTogglePlayback()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    Execute(ERadioControlAction::TogglePlayback);
    return FReply::Handled();
}
