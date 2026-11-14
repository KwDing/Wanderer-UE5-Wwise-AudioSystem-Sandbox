#include "UI/Mapping/WandererMapWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/World.h"

#include "Data/WandererMapMarkerSettings.h"
#include "Framework/WandererPlayerController.h"
#include "Systems/WandererEnvironmentSubsystem.h"
#include "UI/WandererUIAudio.h"

void UWandererMapWidget::SetMapOpen(bool bInOpen, EWandererUIAudioAction InCloseAction)
{
    AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    if (!Player)
    {
        return;
    }
    if (bInOpen)
    {
        Player->SetMapOpen(true);
        return;
    }
    Player->CloseMap(InCloseAction);
}

bool UWandererMapWidget::IsMapOpen() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player && Player->IsMapOpen();
}

bool UWandererMapWidget::OnDoorSelected(FName InID)
{
    AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player && Player->SelectMapDoor(InID);
}

FName UWandererMapWidget::GetSelectedDoor() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetSelectedMapDoor() : NAME_None;
}

bool UWandererMapWidget::ToggleDayNight()
{
    if (!IsMapOpen() || GetLoadingVisibility() == EVisibility::Visible)
    {
        return false;
    }
    UWandererEnvironmentSubsystem* Context =
        UWandererEnvironmentSubsystem::GetEnvironmentSubsystem(this);
    if (!Context)
    {
        return false;
    }
    Context->SetNight(!Context->IsNight());
    OnMapAudio(EWandererUIAudioAction::Choose);
    return true;
}

bool UWandererMapWidget::IsAudioNight() const
{
    const UWandererEnvironmentSubsystem* Context =
        UWandererEnvironmentSubsystem::GetEnvironmentSubsystem(this);
    return Context && Context->IsNight();
}

void UWandererMapWidget::NativeConstruct()
{
    Super::NativeConstruct();
    AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    if (Player)
    {
        if (PlayerWeakPtr.IsValid())
        {
            PlayerWeakPtr->GetMapAudioEvent().Remove(AudioHandle);
        }
        PlayerWeakPtr = Player;
        AudioHandle = Player->GetMapAudioEvent().AddUObject(this, &UWandererMapWidget::OnMapAudio);
    }
}

void UWandererMapWidget::NativeDestruct()
{
    if (PlayerWeakPtr.IsValid())
    {
        PlayerWeakPtr->GetMapAudioEvent().Remove(AudioHandle);
    }
    AudioHandle.Reset();
    PlayerWeakPtr.Reset();
    Super::NativeDestruct();
}

void UWandererMapWidget::OnMapAudio(EWandererUIAudioAction InAction)
{
    UWandererUIAudio::PlayUIAction(this, InAction);
}

FReply UWandererMapWidget::OnDayNightClicked()
{
    ToggleDayNight();
    return FReply::Handled();
}

FSlateColor UWandererMapWidget::GetDayNightColor() const
{
    return IsAudioNight() ? FLinearColor(.08f, .32f, .62f, .90f) : FLinearColor(.32f, .065f, .055f);
}

FText UWandererMapWidget::GetDayNightLabel() const
{
    return FText::FromString(IsAudioNight() ? TEXT("Night") : TEXT("Day"));
}

FReply UWandererMapWidget::OnMapClicked()
{
    SetMapOpen(!IsMapOpen());
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererMapWidget::OnCloseClicked()
{
    SetMapOpen(false);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererMapWidget::OnRescanClicked()
{
    if (AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>())
    {
        Player->RescanMap();
    }
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    return FReply::Handled();
}

FReply UWandererMapWidget::OnTeleportClicked()
{
    AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    if (Player && Player->TeleportSelectedMapDoor())
    {
        UWidgetBlueprintLibrary::SetFocusToGameViewport();
    }
    return FReply::Handled();
}

FReply UWandererMapWidget::OnCancelDoorClicked()
{
    if (AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>())
    {
        Player->ClearMapDoor();
    }
    return FReply::Handled();
}

EVisibility UWandererMapWidget::GetDoorActionVisibility() const
{
    const UWorld* World = GetWorld();
    return World
                   && GetDefault<UWandererMapMarkerSettings>()->FindDoorMarker(
                       *World, GetSelectedDoor())
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

FText UWandererMapWidget::GetDoorLabel() const
{
    const FWandererMapDoorMarker* Marker =
        GetWorld() ? GetDefault<UWandererMapMarkerSettings>()->FindDoorMarker(
                         *GetWorld(), GetSelectedDoor())
                   : nullptr;
    return Marker ? FText::FromString(Marker->Label) : FText::GetEmpty();
}

FText UWandererMapWidget::GetTeleportMessage() const
{
    const AWandererPlayerController* Player = GetOwningPlayer<AWandererPlayerController>();
    return Player ? Player->GetMapTeleportMessage() : FText::GetEmpty();
}
