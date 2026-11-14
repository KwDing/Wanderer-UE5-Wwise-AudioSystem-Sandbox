#include "UI/Widgets/WandererSpeakerWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#include "Data/WandererSpeakerSettings.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererPlayerController.h"
#include "UI/WandererUIAudio.h"

void UWandererSpeakerWidget::SetPanelOpen(bool bOpen)
{
    Execute(bOpen ? ERadioControlAction::OpenPanel : ERadioControlAction::ClosePanel);
    if (!bOpen)
    {
        DestroyPreview();
    }
}

void UWandererSpeakerWidget::NativeDestruct()
{
    Execute(ERadioControlAction::ClosePanel);
    DestroyPreview();
    Super::NativeDestruct();
}

void UWandererSpeakerWidget::NativeTick(const FGeometry& InGeometry, float InDeltaTime)
{
    Super::NativeTick(InGeometry, InDeltaTime);
    RefreshState();
    RefreshPreview();
    RefreshDeviceList();
    const bool bOverInventory = State.bOpen && InventoryBorder && InventoryBorder->IsHovered();
    const bool bOverSelection = GetSelectionVisibility() != EVisibility::Collapsed && ActionBorder
                                && ActionBorder->IsHovered();
    const bool bOverPlacement = State.bPlacing && PlacementBorder && PlacementBorder->IsHovered();
    const bool bOver = State.bVisible && (bOverInventory || bOverSelection || bOverPlacement);
    Execute(bOver ? ERadioControlAction::CursorEntered : ERadioControlAction::CursorLeft);
}

void UWandererSpeakerWidget::ReleaseSlateResources(bool bInReleaseChildren)
{
    Super::ReleaseSlateResources(bInReleaseChildren);
    DestroyPreview();
    DeviceList.Reset();
    DisplayedSources.Reset();
    InventoryBorder.Reset();
    ActionBorder.Reset();
    PlacementBorder.Reset();
    Execute(ERadioControlAction::CursorLeft);
}

bool UWandererSpeakerWidget::CanDeployItem(EWandererSpeakerItem InItem) const
{
    return (InItem == EWandererSpeakerItem::Radio ? State.AvailableRadios : State.AvailableMounted)
           > 0;
}

bool UWandererSpeakerWidget::CanRetrieveSelected() const
{
    const FRadioDeviceSnapshot* Device = FindDevice(State.SelectedID);
    return Device && Device->bRetrievable;
}

const FRadioDeviceSnapshot* UWandererSpeakerWidget::FindDevice(FGuid InID) const
{
    return State.Devices.FindByPredicate(
        [InID](const FRadioDeviceSnapshot& InDevice)
        {
            return InDevice.ID == InID;
        });
}

FText UWandererSpeakerWidget::GetDeviceText(FGuid InID) const
{
    const FRadioDeviceSnapshot* Device = FindDevice(InID);
    if (!Device)
    {
        return FText::FromString(TEXT("Removed"));
    }
    FString Content = Device->ContentID.ToString();
    for (const FWandererSpeakerOption& Option : GetDefault<UWandererSpeakerSettings>()->Options)
    {
        if (Option.ID == Device->ContentID)
        {
            Content = Option.Label;
        }
    }
    return FText::FromString(FString::Printf(TEXT("%s #%d %s | %s | %s"),
        State.SelectedID == InID ? TEXT(">") : TEXT(" "),
        Device->Number,
        Device->Item == EWandererSpeakerItem::Radio ? TEXT("Recorder") : TEXT("Monitor"),
        *Content,
        Device->bPlaying ? TEXT("Playing") : TEXT("Paused / Ready")));
}

EVisibility UWandererSpeakerWidget::GetHUDVisibility() const
{
    return State.bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

FText UWandererSpeakerWidget::GetItemText(EWandererSpeakerItem InItem) const
{
    const bool bRadio = InItem == EWandererSpeakerItem::Radio;
    return FText::FromString(FString::Printf(TEXT("%s\n%d available"),
        bRadio ? TEXT("Recorder") : TEXT("Monitor"),
        bRadio ? State.AvailableRadios : State.AvailableMounted));
}

URadioControlModel* UWandererSpeakerWidget::GetModel() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetRadioControlModel() : nullptr;
}

FText UWandererSpeakerWidget::GetOptionText(FName InID, FString InLabel) const
{
    return FText::FromString((State.SelectedContentID == InID ? TEXT("● ") : TEXT("○ ")) + InLabel);
}

EVisibility UWandererSpeakerWidget::GetPanelVisibility() const
{
    return State.bOpen ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

FText UWandererSpeakerWidget::GetPlaybackText() const
{
    const FRadioDeviceSnapshot* Device = FindDevice(State.SelectedID);
    return Device ? FText::FromString(FString::Printf(TEXT("%s #%d"),
                        Device->bPlaying ? TEXT("Pause") : TEXT("Play"),
                        Device->Number))
                  : FText::FromString(TEXT("Select a device to Play / Pause"));
}

FText UWandererSpeakerWidget::GetSelectionText() const
{
    const FRadioDeviceSnapshot* Device = FindDevice(State.SelectedID);
    if (!Device)
    {
        return FText::GetEmpty();
    }
    const FString Name =
        FString::Printf(TEXT("#%d "), Device->Number)
        + (Device->Item == EWandererSpeakerItem::Radio ? TEXT("Recorder") : TEXT("Monitor"));
    return FText::FromString(
        Name
        + (Device->bRetrievable ? TEXT("  /  Your item") : TEXT("  /  Move closer to retrieve")));
}

EVisibility UWandererSpeakerWidget::GetSelectionVisibility() const
{
    return !State.bOpen && FindDevice(State.SelectedID) && !State.bPlacing
               ? EVisibility::SelfHitTestInvisible
               : EVisibility::Collapsed;
}

FText UWandererSpeakerWidget::GetStatusText() const
{
    return FText::FromString(State.Status);
}

void UWandererSpeakerWidget::DestroyPreview()
{
    RadioBrush.SetResourceObject(nullptr);
    MountedBrush.SetResourceObject(nullptr);
    RadioTexture = nullptr;
    MountedTexture = nullptr;
    if (URadioControlModel* Model = GetModel())
    {
        Model->SetPreviewEnabled(false);
    }
}

bool UWandererSpeakerWidget::Execute(ERadioControlAction InAction)
{
    URadioControlModel* Model = GetModel();
    FRadioControlRequest Request;
    Request.Action = InAction;
    const bool bSucceeded = Model && Model->Execute(Request);
    RefreshState();
    return bSucceeded;
}

void UWandererSpeakerWidget::RefreshPreview()
{
    URadioControlModel* Model = GetModel();
    if (!Model || !State.bOpen || !State.bVisible)
    {
        DestroyPreview();
        return;
    }
    Model->SetPreviewEnabled(true);
    RadioTexture = Model->GetPreview(EWandererSpeakerItem::Radio);
    MountedTexture = Model->GetPreview(EWandererSpeakerItem::Mounted);
    RadioBrush.SetResourceObject(RadioTexture);
    MountedBrush.SetResourceObject(MountedTexture);
}

void UWandererSpeakerWidget::RefreshState()
{
    const bool bWasOpen = State.bOpen;
    URadioControlModel* Model = GetModel();
    State = Model ? Model->GetSnapshot() : FRadioControlSnapshot();
    if (bWasOpen != State.bOpen)
    {
        UWandererUIAudio::PlayUIAction(
            this, State.bOpen ? EWandererUIAudioAction::Open : EWandererUIAudioAction::Close);
        if (!State.bOpen)
        {
            UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Cancel);
        }
    }
}
