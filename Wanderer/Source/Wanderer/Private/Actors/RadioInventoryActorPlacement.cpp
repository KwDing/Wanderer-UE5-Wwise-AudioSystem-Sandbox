#include "Actors/RadioInventoryActor.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Volume.h"
#include "Math/RotationMatrix.h"

#include "Actors/WandererSpeakerActor.h"
#include "Components/ItemPlacementComponent.h"
#include "Data/WandererSpeakerSettings.h"
#include "Framework/WandererPlayerController.h"

namespace RadioPlacementDefaults
{
    constexpr double ProbeHalfDepth = 8;
    constexpr double NormalAgreementMin = 0.95;
    constexpr double FloorNormalThreshold = 0.7;
    constexpr double WallSlopeLimit = 0.25;
    constexpr double SurfaceSeparation = 0.2;
    constexpr double CursorRayLength = 2000;
    constexpr float YawStepDegrees = 15;
    constexpr float FullTurnDegrees = 360;
    constexpr float DebugDuration = 0.06f;
    constexpr float DebugLineWidth = 1.5f;
} // namespace RadioPlacementDefaults

bool ARadioInventoryActor::ApplySurface(const FHitResult& InHit)
{
    bPreviewValid = false;
    AWandererSpeakerActor* Preview = PreviewActorWeakPtr.Get();
    const AWandererPlayerController* Player = GetController();
    if (!Preview || !Player || !Player->GetPawn())
    {
        return false;
    }
    LastPoint = InHit.ImpactPoint;
    LastNormal = InHit.ImpactNormal.GetSafeNormal();
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    FVector Origin = Player->GetPawn()->GetActorLocation();
    if (const ACharacter* Character = Cast<ACharacter>(Player->GetPawn()))
    {
        Origin.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    if (FVector::Dist2D(Origin, LastPoint) > Settings->MaxHorizontalDistance
        || FMath::Abs(Origin.Z - LastPoint.Z) > Settings->MaxVerticalDistance)
    {
        Status = TEXT("Too far from character or more than one floor high.");
        Preview->SetActorHiddenInGame(true);
        return false;
    }
    const bool bFloor = LastNormal.Z > RadioPlacementDefaults::FloorNormalThreshold;
    const EWandererSpeakerItem SurfaceItem =
        PlacementItem == EWandererSpeakerItem::Automatic
            ? (bFloor ? EWandererSpeakerItem::Radio : EWandererSpeakerItem::Mounted)
            : PlacementItem;
    if ((SurfaceItem == EWandererSpeakerItem::Radio && !bFloor)
        || GetAvailableCount(SurfaceItem) <= 0)
    {
        Status = SurfaceItem == EWandererSpeakerItem::Radio && !bFloor
                     ? TEXT("Recorder needs a floor or table.")
                     : TEXT("Retrieve this item before deploying it again.");
        Preview->SetActorHiddenInGame(true);
        return false;
    }
    if (!bFloor && LastNormal.Z > -RadioPlacementDefaults::FloorNormalThreshold
        && FMath::Abs(LastNormal.Z) > RadioPlacementDefaults::WallSlopeLimit)
    {
        Status = TEXT("Choose a flatter floor, wall or ceiling.");
        Preview->SetActorHiddenInGame(true);
        return false;
    }
    return UpdatePlacementTransform(InHit, SurfaceItem);
}

bool ARadioInventoryActor::BeginItemPlacement(EWandererSpeakerItem InItem)
{
    if (!IsGameplayReady())
    {
        Status = TEXT("Close other menus and wait for the scene.");
        return false;
    }
    if (GetAvailableCount(InItem) <= 0)
    {
        Status = TEXT("This item is deployed. Retrieve it to use it again.");
        return false;
    }
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    if (GetSources().Num() >= FMath::Clamp(Settings->MaxSources, 1, 4))
    {
        Status = TEXT("All sources deployed. Retrieve one first.");
        return false;
    }
    RadioMesh = Settings->RadioMesh.LoadSynchronous();
    MountedMesh = Settings->MountedMesh.LoadSynchronous();
    if (!RadioMesh || !MountedMesh)
    {
        Status = TEXT("Speaker models are missing. Check Speaker settings.");
        return false;
    }
    CancelPlacement();
    SelectedSourceWeakPtr.Reset();
    PlacementItem = InItem;
    FActorSpawnParameters Parameters;
    Parameters.Owner = this;
    Parameters.ObjectFlags |= RF_Transient;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PreviewActorWeakPtr = GetWorld()->SpawnActor<AWandererSpeakerActor>(Parameters);
    if (!PreviewActorWeakPtr.IsValid())
    {
        Status = TEXT("Could not create placement preview.");
        return false;
    }
    PreviewActorWeakPtr->SetPreview(true);
    PreviewActorWeakPtr->SetActorHiddenInGame(true);
    PlacementYaw = 0;
    bPreviewValid = false;
    Status = TEXT(
        "Aim at a floor, table, wall or ceiling; R rotates; left click / F places; Esc cancels.");
    return true;
}

void ARadioInventoryActor::CancelPlacement()
{
    if (PreviewActorWeakPtr.IsValid())
    {
        Status = TEXT("Placement cancelled.");
    }
    if (PreviewActorWeakPtr.IsValid())
    {
        PreviewActorWeakPtr->Destroy();
    }
    PreviewActorWeakPtr.Reset();
    bPreviewValid = false;
}

bool ARadioInventoryActor::ConfirmPlacement()
{
    const FName PreviousState = GetRecorderTransportState();
    if (!IsGameplayReady() || !PreviewActorWeakPtr.IsValid() || !bPreviewValid
        || !PreviewSurface(LastPoint, LastNormal))
    {
        return false;
    }
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    if (GetSources().Num() >= FMath::Clamp(Settings->MaxSources, 1, 4))
    {
        return false;
    }
    AWandererSpeakerActor* Source = PreviewActorWeakPtr.Get();
    Source->SetPreview(false);
    SourcesWeakPtrs.RemoveAll(
        [](const TWeakObjectPtr<AWandererSpeakerActor>& EntryWeakPtr)
        {
            return !EntryWeakPtr.IsValid();
        });
    int32 Number = 1;
    while (SourcesWeakPtrs.ContainsByPredicate(
        [Number](const TWeakObjectPtr<AWandererSpeakerActor>& EntryWeakPtr)
        {
            return EntryWeakPtr.IsValid() && EntryWeakPtr->GetDeviceNumber() == Number;
        }))
    {
        ++Number;
    }
    Source->SetDeviceNumber(Number);
    SourcesWeakPtrs.Add(Source);
    SelectedSourceWeakPtr = Source;
    if (PendingOption.IsNone() && !Settings->Options.IsEmpty())
    {
        PendingOption = Settings->Options[0].ID;
    }
    SetSourceIntent(*Source, PendingOption, false);
    UpdatePairPlayback(0);
    PreviewActorWeakPtr.Reset();
    bPreviewValid = false;
    Status = TEXT("Placed. Select a device to retrieve it or change playback.");
    LogRecorderAction(TEXT("Place"), TEXT("Accepted"), PreviousState, Status);
    return true;
}

bool ARadioInventoryActor::ConfirmPlacementFromPointer()
{
    return !bCursorOverPanel && IsPlacing() && ConfirmPlacement();
}

bool ARadioInventoryActor::FindSurface(FVector InStart, FVector InEnd, FHitResult& OutHit) const
{
    const AWandererPlayerController* Player = GetController();
    Placement->ConfigureQuery(Player ? Player->GetPawn() : nullptr,
        PreviewActorWeakPtr.Get(),
        AWandererSpeakerActor::StaticClass());
    return Placement->FindSurface(InStart, InEnd, OutHit);
}

bool ARadioInventoryActor::PreviewSurface(FVector InSurfacePoint, FVector InSurfaceNormal)
{
    bPreviewValid = false;
    if (!IsPlacing() || !IsGameplayReady() || InSurfacePoint.ContainsNaN()
        || InSurfaceNormal.ContainsNaN() || InSurfaceNormal.IsNearlyZero())
    {
        return false;
    }
    const FVector Normal = InSurfaceNormal.GetSafeNormal();
    FHitResult Hit;
    if (!FindSurface(InSurfacePoint + Normal * RadioPlacementDefaults::ProbeHalfDepth,
            InSurfacePoint - Normal * RadioPlacementDefaults::ProbeHalfDepth,
            Hit)
        || FVector::DotProduct(Hit.ImpactNormal, Normal)
               < RadioPlacementDefaults::NormalAgreementMin)
    {
        Status = TEXT("No solid support at the chosen surface.");
        PreviewActorWeakPtr->SetActorHiddenInGame(true);
        return false;
    }
    return ApplySurface(Hit);
}

void ARadioInventoryActor::RotatePreview()
{
    if (!IsPlacing())
    {
        return;
    }
    PlacementYaw = FMath::Fmod(PlacementYaw + RadioPlacementDefaults::YawStepDegrees,
        RadioPlacementDefaults::FullTurnDegrees);
    PreviewSurface(LastPoint, LastNormal);
}

void ARadioInventoryActor::UpdatePlacementPreview()
{
    if (!IsPlacing())
    {
        return;
    }
    if (bCursorOverPanel)
    {
        return;
    }
    FVector Start;
    FVector Direction;
    AWandererPlayerController* Player = GetController();
    if (!Player->DeprojectMousePositionToWorld(Start, Direction))
    {
        FRotator View;
        Player->GetPlayerViewPoint(Start, View);
        Direction = View.Vector();
    }
    FHitResult Hit;
    if (!FindSurface(Start, Start + Direction * RadioPlacementDefaults::CursorRayLength, Hit))
    {
        bPreviewValid = false;
        PreviewActorWeakPtr->SetActorHiddenInGame(true);
        Status = TEXT("Aim at a solid surface.");
        return;
    }
    ApplySurface(Hit);
    if (!PreviewActorWeakPtr->IsHidden())
    {
        DrawDebugBox(GetWorld(),
            PreviewActorWeakPtr->GetActorLocation(),
            PreviewActorWeakPtr->GetPlacementExtent(),
            PreviewActorWeakPtr->GetActorQuat(),
            bPreviewValid ? FColor::Green : FColor::Red,
            false,
            RadioPlacementDefaults::DebugDuration,
            0,
            RadioPlacementDefaults::DebugLineWidth);
    }
}

bool ARadioInventoryActor::UpdatePlacementTransform(
    const FHitResult& InHit, EWandererSpeakerItem InSurfaceItem)
{
    AWandererSpeakerActor* Preview = PreviewActorWeakPtr.Get();
    const UWandererSpeakerSettings* Settings = GetDefault<UWandererSpeakerSettings>();
    const bool bFloor = LastNormal.Z > RadioPlacementDefaults::FloorNormalThreshold;
    const bool bRadio = InSurfaceItem == EWandererSpeakerItem::Radio;
    Preview->ConfigureVisual(
        bRadio ? RadioMesh : MountedMesh, bRadio ? Settings->RadioSize : Settings->MountedSize);
    Preview->SetItemType(InSurfaceItem);
    const FQuat Rotation =
        bFloor
            ? FRotationMatrix::MakeFromZX(LastNormal,
                  FRotator(0, GetController()->GetControlRotation().Yaw + PlacementYaw, 0).Vector())
                  .ToQuat()
            : FRotationMatrix::MakeFromX(LastNormal).ToQuat()
                  * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(PlacementYaw));
    const FVector Extent = Preview->GetPlacementExtent();
    const double Depth = bFloor ? Extent.Z : Extent.X;
    const FTransform Transform(
        Rotation, LastPoint + LastNormal * (Depth + RadioPlacementDefaults::SurfaceSeparation));
    Preview->SetActorTransform(Transform);
    Preview->SetActorHiddenInGame(false);
    const bool bBlocked = Placement->IsBlocked(Transform, Extent, Preview);
    bPreviewValid = !bBlocked && Placement->ValidateSupport(InHit, Transform, Extent);
    Status = bPreviewValid ? TEXT("Ready: left click / F / Place confirms; R rotates; Esc cancels.")
                           : TEXT("Blocked or edge overhang. Choose a clear supported surface.");
    return bPreviewValid;
}
