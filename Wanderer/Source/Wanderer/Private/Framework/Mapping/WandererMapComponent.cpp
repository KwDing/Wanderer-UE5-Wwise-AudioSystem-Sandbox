#include "Framework/Mapping/WandererMapComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Volume.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"

#include "Core/NaturalSurfaceUtils.h"
#include "Data/MapScanData.h"
#include "Interfaces/CharacterSurfaceProvider.h"

UWandererMapComponent::UWandererMapComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UWandererMapComponent::Rescan()
{
    ScanCharacterWeakPtr.Reset();
    Snapshot.CellSize = CellSize;
    Snapshot.Cells.Reset();
    Snapshot.CellLookup.Reset();
    Snapshot.Entrances.Reset();
    Snapshot.MapBounds = FBox2D(ForceInit);
    NextCell = 0;
    bNeedsScan = true;
    Snapshot.bScanning = false;
    Snapshot.bTruncated = false;
    Snapshot.Regions.Reset();
    SurveyWater.Reset();
    SurveyQuery = FCollisionQueryParams();
    Snapshot.bSurveying = false;
    NextSurveyCell = 0;
}

bool UWandererMapComponent::IsScanning() const
{
    return Snapshot.bScanning || Snapshot.bSurveying;
}

bool UWandererMapComponent::HasCell(const FIntPoint& Key) const
{
    return Snapshot.CellLookup.Contains(Key);
}

FIntPoint UWandererMapComponent::GetCellKey(const FVector& InPosition) const
{
    return FIntPoint(FMath::RoundToInt((InPosition.X - Snapshot.Seed.X) / CellSize),
        FMath::RoundToInt((InPosition.Y - Snapshot.Seed.Y) / CellSize));
}

void UWandererMapComponent::TickComponent(
    float InDeltaTime, ELevelTick InTickType, FActorComponentTickFunction* InThisTickFunction)
{
    Super::TickComponent(InDeltaTime, InTickType, InThisTickFunction);
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character || !Controller->IsLocalController())
    {
        if (ScanCharacterWeakPtr.IsValid() || !Snapshot.Cells.IsEmpty())
        {
            Rescan();
        }
        return;
    }
    if (!bNeedsScan && ScanCharacterWeakPtr.Get() != Character)
    {
        Rescan();
    }
    if (bNeedsScan && !StartScan(*Character))
    {
        return;
    }
    if (!Snapshot.bScanning && !Snapshot.bSurveying)
    {
        return;
    }
    // The deadline bounds incremental cells, not scene enumeration or the entrance pass.
    const double Deadline = FPlatformTime::Seconds() + UE::Wanderer::MapScan::FrameBudgetSeconds;
    while (Snapshot.bSurveying && FPlatformTime::Seconds() < Deadline)
    {
        SampleSurveyCell();
    }
    while (Snapshot.bScanning && NextCell < Snapshot.Cells.Num()
           && Snapshot.Cells.Num() < MaximumCells && FPlatformTime::Seconds() < Deadline)
    {
        ExpandCell(NextCell++);
    }
    if (Snapshot.bScanning
        && (NextCell >= Snapshot.Cells.Num() || Snapshot.Cells.Num() >= MaximumCells))
    {
        Snapshot.bTruncated |= Snapshot.Cells.Num() >= MaximumCells;
        Snapshot.bScanning = false;
        FindEntrances();
    }
}

void UWandererMapComponent::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    SetComponentTickEnabled(false);
    Rescan();
    bNeedsScan = false;
    Super::EndPlay(InEndPlayReason);
}

bool UWandererMapComponent::StartScan(ACharacter& InCharacter)
{
    if (!InCharacter.GetCharacterMovement()->IsMovingOnGround())
    {
        return false;
    }
    ScanCharacterWeakPtr = &InCharacter;
    Radius = InCharacter.GetCapsuleComponent()->GetScaledCapsuleRadius();
    HalfHeight = InCharacter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    StepHeight = InCharacter.GetCharacterMovement()->MaxStepHeight;
    WalkableFloorZ = InCharacter.GetCharacterMovement()->GetWalkableFloorZ();
    Snapshot.Seed = InCharacter.GetActorLocation() - FVector(0, 0, HalfHeight);
    FVector Floor;
    if (!FindFloor(Snapshot.Seed, Floor))
    {
        return false;
    }
    AddCell(Floor);
    bNeedsScan = false;
    Snapshot.bScanning = true;
    StartSurvey();
    return true;
}

bool UWandererMapComponent::FindFloor(const FVector& InPosition, FVector& OutFloor) const
{
    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(WandererMapFloor), false, ScanCharacterWeakPtr.Get());
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit,
            InPosition + FVector(0, 0, StepHeight + UE::Wanderer::MapScan::CapsuleClearance),
            InPosition - FVector(0, 0, StepHeight + UE::Wanderer::MapScan::CapsuleClearance),
            ECC_Pawn,
            Params))
    {
        return false;
    }
    if (Hit.ImpactNormal.Z < WalkableFloorZ
        || FMath::Abs(Hit.ImpactPoint.Z - InPosition.Z) > StepHeight)
    {
        return false;
    }
    OutFloor = Hit.ImpactPoint;
    const FVector Center =
        OutFloor + FVector(0, 0, HalfHeight + UE::Wanderer::MapScan::CapsuleClearance);
    return !GetWorld()->OverlapBlockingTestByChannel(Center,
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius, HalfHeight),
        Params);
}

bool UWandererMapComponent::CanTraverse(const FVector& InFrom, const FVector& InTo) const
{
    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(WandererMapPassage), false, ScanCharacterWeakPtr.Get());
    const double Height =
        FMath::Max(InFrom.Z, InTo.Z) + HalfHeight + UE::Wanderer::MapScan::CapsuleClearance;
    FHitResult Hit;
    return !GetWorld()->SweepSingleByChannel(Hit,
        FVector(InFrom.X, InFrom.Y, Height),
        FVector(InTo.X, InTo.Y, Height),
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius, HalfHeight),
        Params);
}

void UWandererMapComponent::ExpandCell(int32 Index)
{
    const FVector From = Snapshot.Cells[Index];
    const FVector Directions[] = {
        FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0)};
    for (const FVector& Direction : Directions)
    {
        const FVector Target = From + Direction * CellSize;
        if (Snapshot.CellLookup.Contains(GetCellKey(Target)))
        {
            continue;
        }
        if (FVector::DistSquared2D(Target, Snapshot.Seed) > FMath::Square(ScanRadius))
        {
            Snapshot.bTruncated = true;
            continue;
        }
        FVector Floor;
        if (FindFloor(Target, Floor) && CanTraverse(From, Floor))
        {
            AddCell(Floor);
        }
    }
}

void UWandererMapComponent::AddCell(const FVector& InFloor)
{
    Snapshot.CellLookup.Add(GetCellKey(InFloor), Snapshot.Cells.Add(InFloor));
    Snapshot.MapBounds += FVector2D(InFloor.X, InFloor.Y);
}

void UWandererMapComponent::FindEntrances()
{
    for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components(*Actor);
        for (const UStaticMeshComponent* Component : Components)
        {
            if (!Component->GetStaticMesh()
                || !Component->GetStaticMesh()->GetName().Contains(TEXT("Door")))
            {
                continue;
            }
            FVector Min;
            FVector Max;
            Component->GetLocalBounds(Min, Max);
            const FTransform Transform = Component->GetComponentTransform();
            const FVector Center = Transform.TransformPosition(
                FVector((Min.X + Max.X) / 2, (Min.Y + Max.Y) / 2, Min.Z));
            const FVector Axis = Transform.TransformVectorNoScale(
                (Max.X - Min.X < Max.Y - Min.Y) ? FVector::ForwardVector : FVector::RightVector);
            FVector Front;
            FVector Back;
            if (!FindFloor(Center + Axis * (Radius + CellSize), Front)
                || !FindFloor(Center - Axis * (Radius + CellSize), Back))
            {
                continue;
            }
            if (!HasCell(GetCellKey(Front)) || !HasCell(GetCellKey(Back))
                || !CanTraverse(Front, Back))
            {
                continue;
            }
            if (!Snapshot.Entrances.ContainsByPredicate(
                    [Center](const FVector& Existing)
                    {
                        return FVector::DistSquared2D(Center, Existing)
                               < UE::Wanderer::MapScan::EntranceMergeDistanceSquared;
                    }))
            {
                Snapshot.Entrances.Add(Center);
            }
        }
    }
}
