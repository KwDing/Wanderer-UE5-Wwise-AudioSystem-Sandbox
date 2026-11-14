#include "Framework/WandererPlayerController.h"

#include "Engine/World.h"
#include "Framework/WandererGameState.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "Framework/CharacterSelectionModel.h"
#include "Framework/WandererLoadingComponent.h"
#include "Framework/Surface/WandererSurfaceComponent.h"
#include "Framework/Mapping/WandererMapComponent.h"

UCharacterSelectionModel* AWandererPlayerController::GetCharacterSelectionModel() const
{
    return CharacterSelectionModel;
}

void AWandererPlayerController::RefreshContactRegions()
{
    if (IsFrameworkActive() && IsValid(SurfaceComponent))
    {
        SurfaceComponent->RefreshSurfaceRegions();
    }
}

FName AWandererPlayerController::ResolveSurfaceName(const FHitResult& InHit) const
{
    return IsValid(SurfaceComponent) ? SurfaceComponent->ResolveSurface(InHit)
                                     : FName(TEXT("ground"));
}

FCharacterSurfaceSnapshot AWandererPlayerController::QueryContactSurface(
    const FHitResult* InHit, const FVector& InFeet) const
{
    FCharacterSurfaceSnapshot Result;
    if (!IsValid(SurfaceComponent))
    {
        return Result;
    }
    if (InHit && InHit->bBlockingHit)
    {
        Result.Surface = SurfaceComponent->ResolveSurface(*InHit);
        Result.PhysicalSurface = UPhysicalMaterial::DetermineSurfaceType(InHit->PhysMaterial.Get());
        Result.Wetness = SurfaceComponent->ResolveContactWetness(*InHit, InFeet);
        return Result;
    }
    Result.Surface = SurfaceComponent->GetCurrentSurface();
    Result.PhysicalSurface = SurfaceComponent->GetCurrentPhysicalSurface();
    Result.Wetness = SurfaceComponent->GetCurrentWetness();
    return Result;
}

bool AWandererPlayerController::IsEntryLoading() const
{
    return !bFrameworkStopped && (!LoadingComponent || LoadingComponent->IsLoading());
}

bool AWandererPlayerController::AreEntryCharactersReady() const
{
    return CharacterSelectionModel && CharacterSelectionModel->AreResourcesReady() && GetWorld()
           && GetWorld()->GetGameState<AWandererGameState>();
}

void AWandererPlayerController::SetEntryMapScanEnabled(bool bInEnabled)
{
    if (IsFrameworkActive() && MapComponent)
    {
        MapComponent->SetComponentTickEnabled(bInEnabled);
    }
}

void AWandererPlayerController::StartEntryMapScan()
{
    if (IsFrameworkActive() && MapComponent)
    {
        MapComponent->Rescan();
        MapComponent->SetComponentTickEnabled(true);
    }
}

bool AWandererPlayerController::IsEntryMapReady() const
{
    return MapComponent && MapComponent->GetReachableCellCount() > 0 && !MapComponent->IsScanning();
}

bool AWandererPlayerController::SetEntryLoadingState(bool bInLoading)
{
    AWandererGameState* State =
        GetWorld() ? GetWorld()->GetGameState<AWandererGameState>() : nullptr;
    return IsFrameworkActive() && State
           && State->SetPlayState(
               bInLoading ? EWandererPlayState::LoadMap : EWandererPlayState::Normal);
}

bool AWandererPlayerController::SetCharacterSelectionState(bool bInOpen)
{
    return IsFrameworkActive() && !IsEntryLoading() && ObservedGameStateWeakPtr.IsValid()
           && ObservedGameStateWeakPtr->SetPlayState(
               bInOpen ? EWandererPlayState::UI : EWandererPlayState::Normal);
}
