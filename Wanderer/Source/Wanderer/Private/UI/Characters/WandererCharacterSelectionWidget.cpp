#include "UI/Characters/WandererCharacterSelectionWidget.h"

#include "Engine/TextureRenderTarget2D.h"
#include "InputCoreTypes.h"

#include "Framework/CharacterSelectionModel.h"
#include "Framework/WandererPlayerController.h"
#include "UI/WandererUIAudio.h"

void UWandererCharacterSelectionWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    UnbindModel();
    PortraitBrush.SetResourceObject(nullptr);
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UWandererCharacterSelectionWidget::NativeConstruct()
{
    Super::NativeConstruct();
    UnbindModel();
    ModelWeakPtr = GetModel();
    if (ModelWeakPtr.IsValid())
    {
        ChangedHandle = ModelWeakPtr->OnChanged.AddUObject(
            this, &UWandererCharacterSelectionWidget::OnModelChanged);
        OnModelChanged();
    }
}

void UWandererCharacterSelectionWidget::NativeDestruct()
{
    UnbindModel();
    PortraitBrush.SetResourceObject(nullptr);
    Super::NativeDestruct();
}

FReply UWandererCharacterSelectionWidget::NativeOnKeyDown(
    const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    const UCharacterSelectionModel* Model = GetModel();
    return Model && Model->IsOpen() && KeyEvent.GetKey() == EKeys::Escape
               ? OnCancelSelection()
               : Super::NativeOnKeyDown(Geometry, KeyEvent);
}

UCharacterSelectionModel* UWandererCharacterSelectionWidget::GetModel() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetCharacterSelectionModel() : nullptr;
}

void UWandererCharacterSelectionWidget::OnModelChanged()
{
    const UCharacterSelectionModel* Model = GetModel();
    PortraitBrush.SetResourceObject(Model ? Model->GetPortrait() : nullptr);
    const bool bOpen = Model && Model->IsOpen();
    if (bOpen != bWasOpen && Model && Model->GetOptionCount() > 0)
    {
        UWandererUIAudio::PlayUIAction(
            this, bOpen ? EWandererUIAudioAction::Open : EWandererUIAudioAction::Close);
    }
    bWasOpen = bOpen;
    if (bOpen && IsInViewport())
    {
        SetKeyboardFocus();
    }
}

void UWandererCharacterSelectionWidget::UnbindModel()
{
    if (ModelWeakPtr.IsValid())
    {
        ModelWeakPtr->OnChanged.Remove(ChangedHandle);
    }
    ChangedHandle.Reset();
    ModelWeakPtr.Reset();
}

FReply UWandererCharacterSelectionWidget::OnOpenSelection()
{
    if (UCharacterSelectionModel* Model = GetModel())
    {
        Model->Open();
    }
    return FReply::Handled();
}

FReply UWandererCharacterSelectionWidget::OnCancelSelection()
{
    UCharacterSelectionModel* Model = GetModel();
    if (Model && Model->Close())
    {
        UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Cancel);
    }
    return FReply::Handled();
}

FReply UWandererCharacterSelectionWidget::OnSelectPreview(int32 InIndex)
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    if (UCharacterSelectionModel* Model = GetModel())
    {
        Model->Preview(InIndex);
    }
    return FReply::Handled();
}

FReply UWandererCharacterSelectionWidget::OnConfirmSelection()
{
    UWandererUIAudio::PlayUIAction(this, EWandererUIAudioAction::Choose);
    if (UCharacterSelectionModel* Model = GetModel())
    {
        Model->Choose(Model->GetSelectedIndex());
    }
    return FReply::Handled();
}

EVisibility UWandererCharacterSelectionWidget::GetPanelVisibility() const
{
    const UCharacterSelectionModel* Model = GetModel();
    return Model && Model->IsOpen() ? EVisibility::Visible : EVisibility::Collapsed;
}
EVisibility UWandererCharacterSelectionWidget::GetButtonVisibility() const
{
    return GetPanelVisibility() == EVisibility::Visible ? EVisibility::Collapsed
                                                        : EVisibility::Visible;
}
FText UWandererCharacterSelectionWidget::GetSelectedName() const
{
    const UCharacterSelectionModel* Model = GetModel();
    return Model ? Model->GetOptionName(Model->GetSelectedIndex()) : FText::GetEmpty();
}
FText UWandererCharacterSelectionWidget::GetStatusText() const
{
    const UCharacterSelectionModel* Model = GetModel();
    return Model ? Model->GetStatus() : FText::GetEmpty();
}
FText UWandererCharacterSelectionWidget::GetRowText(int32 InIndex) const
{
    const UCharacterSelectionModel* Model = GetModel();
    if (!Model)
    {
        return FText::GetEmpty();
    }
    return FText::FromString((Model->GetSelectedIndex() == InIndex ? TEXT(">  ") : TEXT("    "))
                             + Model->GetOptionName(InIndex).ToString());
}
bool UWandererCharacterSelectionWidget::CanChoose() const
{
    const UCharacterSelectionModel* Model = GetModel();
    return Model && Model->CanChoose();
}
