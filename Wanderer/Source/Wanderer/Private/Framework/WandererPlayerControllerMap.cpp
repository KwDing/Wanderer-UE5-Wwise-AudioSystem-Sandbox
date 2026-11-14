#include "Framework/WandererPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "Data/WandererMapMarkerSettings.h"
#include "Framework/Mapping/WandererMapComponent.h"
#include "Framework/WandererLoadingComponent.h"

const FMapScanSnapshot* AWandererPlayerController::GetMapScanSnapshot() const
{
    return !bMapStopped && IsValid(MapComponent) ? &MapComponent->GetSnapshot() : nullptr;
}

FMapPlayerSnapshot AWandererPlayerController::GetMapPlayerSnapshot() const
{
    FMapPlayerSnapshot Result;
    const APawn* CurrentPawn = GetPawn();
    if (IsMapInteractionReady() && IsValid(CurrentPawn))
    {
        Result.Location = CurrentPawn->GetActorLocation();
        Result.Forward = CurrentPawn->GetActorForwardVector();
        Result.bVisible = true;
    }
    return Result;
}

void AWandererPlayerController::CloseMap(EWandererUIAudioAction InAction)
{
    if (!bMapOpen || bMapStopped)
    {
        return;
    }
    bMapOpen = false;
    SelectedMapDoor = NAME_None;
    MapTeleportMessage = FText::GetEmpty();
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Close);
    if (!bMapStopped)
    {
        MapAudioEvent.Broadcast(InAction);
    }
}

void AWandererPlayerController::RescanMap()
{
    if (!IsMapInteractionReady() || !IsValid(MapComponent))
    {
        return;
    }
    MapComponent->Rescan();
    RefreshContactRegions();
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Choose);
}

void AWandererPlayerController::ClearMapDoor()
{
    if (bMapStopped)
    {
        return;
    }
    SelectedMapDoor = NAME_None;
    MapTeleportMessage = FText::GetEmpty();
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Cancel);
}

bool AWandererPlayerController::TeleportSelectedMapDoor()
{
    if (!IsMapInteractionReady() || !bMapOpen)
    {
        return false;
    }
    const TWeakObjectPtr<APawn> RequestedPawnWeakPtr = GetPawn();
    const TWeakObjectPtr<UWorld> RequestedWorldWeakPtr = GetWorld();
    const FName RequestedDoor = SelectedMapDoor;
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Choose);
    // Synchronous listeners may close the map, travel, or replace the possessed entity.
    if (!IsMapInteractionReady() || !bMapOpen || !RequestedPawnWeakPtr.IsValid()
        || RequestedPawnWeakPtr.Get() != GetPawn() || !RequestedWorldWeakPtr.IsValid()
        || RequestedWorldWeakPtr.Get() != GetWorld() || RequestedDoor != SelectedMapDoor)
    {
        return false;
    }
    FText Failure;
    const bool bTeleported = TeleportToMapDoor(RequestedDoor, Failure);
    if (!bMapStopped)
    {
        MapTeleportMessage = Failure;
    }
    return bTeleported;
}

bool AWandererPlayerController::IsMapInteractionReady() const
{
    return !bMapStopped && IsLocalController() && LoadingComponent && !LoadingComponent->IsLoading()
           && !IsCharacterSelectionOpen();
}

void AWandererPlayerController::SetMapOpen(bool bOpen)
{
    if (!IsMapInteractionReady() || bMapOpen == bOpen)
    {
        return;
    }
    if (!bOpen)
    {
        CloseMap(EWandererUIAudioAction::Cancel);
        return;
    }
    bMapOpen = true;
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Open);
}

void AWandererPlayerController::ToggleMap()
{
    SetMapOpen(!IsMapOpen());
}

bool AWandererPlayerController::IsMapOpen() const
{
    return !bMapStopped && bMapOpen;
}

bool AWandererPlayerController::SelectMapDoor(FName ID)
{
    const UWorld* World = GetWorld();
    if (!IsMapInteractionReady() || !bMapOpen || !World
        || !GetDefault<UWandererMapMarkerSettings>()->FindDoorMarker(*World, ID))
    {
        return false;
    }
    SelectedMapDoor = ID;
    MapTeleportMessage = FText::GetEmpty();
    MapAudioEvent.Broadcast(EWandererUIAudioAction::Choose);
    return !bMapStopped;
}

FName AWandererPlayerController::GetSelectedMapDoor() const
{
    return bMapStopped ? NAME_None : SelectedMapDoor;
}

bool AWandererPlayerController::TeleportToMapDoor(FName ID, FText& OutFailure)
{
    const FWandererMapDoorMarker* Marker =
        GetWorld() ? GetDefault<UWandererMapMarkerSettings>()->FindDoorMarker(*GetWorld(), ID)
                   : nullptr;
    if (bMapStopped || !Marker)
    {
        OutFailure = FText::FromString(TEXT("This door is not configured for the current scene."));
        return false;
    }
    const bool bTeleported =
        TeleportToLocation(Marker->DestinationFloor, Marker->ArrivalRotation, OutFailure);
    if (bTeleported && !bMapStopped)
    {
        CloseMap(EWandererUIAudioAction::Silent);
    }
    return bTeleported;
}
