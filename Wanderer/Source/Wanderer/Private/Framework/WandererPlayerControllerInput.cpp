#include "Framework/WandererPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererLoadingComponent.h"

bool AWandererPlayerController::IsRadioInteractionReady() const
{
    const AWandererGameState* State =
        GetWorld() ? GetWorld()->GetGameState<AWandererGameState>() : nullptr;
    return IsFrameworkActive() && IsLocalController() && GetPawn() && State
           && State->GetPlayState() == EWandererPlayState::Normal && LoadingComponent
           && !LoadingComponent->IsLoading() && !IsMapOpen();
}

bool AWandererPlayerController::IsRadioHUDVisible() const
{
    return IsFrameworkActive() && LoadingComponent && !LoadingComponent->IsLoading()
           && !IsCharacterSelectionOpen() && !IsMapOpen();
}

void AWandererPlayerController::SetSpeakerPanelOpen(bool bOpen)
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    FRadioControlRequest Request;
    Request.Action = bOpen ? ERadioControlAction::OpenPanel : ERadioControlAction::ClosePanel;
    RadioControlModel->Execute(Request);
}

void AWandererPlayerController::ToggleItemInteraction()
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    SetSpeakerPanelOpen(!RadioControlModel->GetSnapshot().bOpen);
}

void AWandererPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (!InputComponent)
    {
        return;
    }
    InputComponent->BindKey(
        EKeys::B, IE_Pressed, this, &AWandererPlayerController::StartSpeakerPlacement);
    InputComponent->BindKey(
        EKeys::F, IE_Pressed, this, &AWandererPlayerController::ConfirmSpeakerPlacement);
    InputComponent
        ->BindKey(EKeys::LeftMouseButton,
            IE_Pressed,
            this,
            &AWandererPlayerController::ConfirmSpeakerPlacementWithMouse)
        .bConsumeInput = false;
    InputComponent->BindKey(
        EKeys::R, IE_Pressed, this, &AWandererPlayerController::RotateSpeakerPlacement);
    InputComponent->BindKey(
        EKeys::E, IE_Pressed, this, &AWandererPlayerController::ToggleItemInteraction);
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AWandererPlayerController::ToggleMap);
    InputComponent
        ->BindKey(EKeys::Escape, IE_Pressed, this, &AWandererPlayerController::TogglePauseMenu)
        .bExecuteWhenPaused = true;
}

void AWandererPlayerController::StartSpeakerPlacement()
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::BeginPlacement;
    if (RadioControlModel->Execute(Request))
    {
        SetSpeakerPanelOpen(true);
    }
}

void AWandererPlayerController::ConfirmSpeakerPlacement()
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::ConfirmPlacement;
    RadioControlModel->Execute(Request);
}

void AWandererPlayerController::ConfirmSpeakerPlacementWithMouse()
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    FRadioControlRequest Request;
    Request.Action = RadioControlModel->GetSnapshot().bPlacing
                         ? ERadioControlAction::ConfirmFromPointer
                         : ERadioControlAction::SelectAtCursor;
    RadioControlModel->Execute(Request);
}

void AWandererPlayerController::TogglePauseMenu()
{
    if (!IsFrameworkActive() || !ObservedGameStateWeakPtr.IsValid()
        || LoadingComponent->IsLoading())
    {
        return;
    }
    const EWandererPlayState Current = ObservedGameStateWeakPtr->GetPlayState();
    if (Current == EWandererPlayState::PauseMenu)
    {
        ObservedGameStateWeakPtr->SetPlayState(EWandererPlayState::Normal);
    }
    else if (Current == EWandererPlayState::Normal)
    {
        ObservedGameStateWeakPtr->SetPlayState(EWandererPlayState::PauseMenu);
    }
}

void AWandererPlayerController::RotateSpeakerPlacement()
{
    if (!IsFrameworkActive() || !IsValid(RadioControlModel))
    {
        return;
    }
    FRadioControlRequest Request;
    Request.Action = ERadioControlAction::RotatePlacement;
    RadioControlModel->Execute(Request);
}
