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
#include "Data/DemoSurfaceData.h"
#include "Interfaces/CharacterSurfaceProvider.h"

void UWandererMapComponent::StartSurvey()
{
    SurveyBounds = FBox(ForceInit);
    SurveyQuery = FCollisionQueryParams(
        SCENE_QUERY_STAT(WandererMapSurvey), true, ScanCharacterWeakPtr.Get());
    SurveyQuery.bReturnFaceIndex = true;
    SurveyQuery.bReturnPhysicalMaterial = true;
    FBox VisibleBounds(ForceInit);
    GatherSurveyBounds(VisibleBounds);
    if (!SurveyBounds.IsValid)
    {
        SurveyBounds = VisibleBounds;
    }
    if (!SurveyBounds.IsValid)
    {
        return;
    }
    SurveyBounds.Min.Z = FMath::Min(VisibleBounds.Min.Z, Snapshot.Seed.Z)
                         - UE::Wanderer::MapScan::VerticalTracePadding;
    SurveyBounds.Max.Z = FMath::Max(VisibleBounds.Max.Z, Snapshot.Seed.Z)
                         + UE::Wanderer::MapScan::VerticalTracePadding;
    // Limit unknown/open scenes too; never allocate a raster proportional to an unbounded world.
    SurveyBounds.Min.X = FMath::Max(SurveyBounds.Min.X, Snapshot.Seed.X - ScanRadius);
    SurveyBounds.Max.X = FMath::Min(SurveyBounds.Max.X, Snapshot.Seed.X + ScanRadius);
    SurveyBounds.Min.Y = FMath::Max(SurveyBounds.Min.Y, Snapshot.Seed.Y - ScanRadius);
    SurveyBounds.Max.Y = FMath::Min(SurveyBounds.Max.Y, Snapshot.Seed.Y + ScanRadius);
    const FVector Extent = SurveyBounds.GetSize();
    if (Extent.ContainsNaN() || !FMath::IsFinite(Extent.X * Extent.Y) || Extent.X <= 0.0
        || Extent.Y <= 0.0)
    {
        return;
    }
    SurveyCellSize = FMath::Max(UE::Wanderer::MapScan::MinimumSurveyCellSize,
        FMath::Sqrt(Extent.X * Extent.Y / UE::Wanderer::MapScan::TargetSurveyCells));
    SurveyDimensions = FIntPoint(
        FMath::CeilToInt(Extent.X / SurveyCellSize), FMath::CeilToInt(Extent.Y / SurveyCellSize));
    Snapshot.MapBounds += FVector2D(SurveyBounds.Min);
    Snapshot.MapBounds += FVector2D(SurveyBounds.Max);
    Snapshot.bSurveying = SurveyDimensions.X > 0 && SurveyDimensions.Y > 0;
}

void UWandererMapComponent::GatherSurveyBounds(FBox& OutVisibleBounds)
{
    for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
    {
        if (Actor->ActorHasTag(TEXT("TrialBoundary")))
        {
            SurveyBounds += Actor->GetComponentsBoundingBox(true);
        }
        if (Actor->IsHidden() || Actor->IsA<AVolume>() || Actor->IsA<APawn>())
        {
            SurveyQuery.AddIgnoredActor(*Actor);
        }
        TInlineComponentArray<UStaticMeshComponent*> Components(*Actor);
        for (UStaticMeshComponent* Component : Components)
        {
            GatherSurveyMesh(*Component, OutVisibleBounds);
        }
    }
}

void UWandererMapComponent::GatherSurveyMesh(UStaticMeshComponent& InMesh, FBox& OutVisibleBounds)
{
    if (!InMesh.IsVisible() || InMesh.bHiddenInGame)
    {
        SurveyQuery.AddIgnoredComponent(&InMesh);
        return;
    }
    if (!InMesh.GetStaticMesh())
    {
        return;
    }
    const FString Name = InMesh.GetStaticMesh()->GetName();
    if (InMesh.ComponentHasTag(TEXT("WaterSurface")))
    {
        SurveyWater.Add(&InMesh);
    }
    // Foliage canopy and decorative sky/backdrop must not mask the ground below.
    if (Name.Contains(TEXT("Tree")) || Name.Contains(TEXT("Bush")) || Name.Contains(TEXT("Grass"))
        || InMesh.Bounds.BoxExtent.GetMax() > UE::Wanderer::MapScan::BackdropExtent)
    {
        SurveyQuery.AddIgnoredComponent(&InMesh);
    }
    else
    {
        OutVisibleBounds += InMesh.Bounds.GetBox();
    }
}

void UWandererMapComponent::SampleSurveyCell()
{
    const int32 Column = NextSurveyCell % SurveyDimensions.X;
    const int32 Row = NextSurveyCell / SurveyDimensions.X;
    const FVector Start(SurveyBounds.Min.X + (Column + 0.5) * SurveyCellSize,
        SurveyBounds.Min.Y + (Row + 0.5) * SurveyCellSize,
        SurveyBounds.Max.Z);
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(
            Hit, Start, FVector(Start.X, Start.Y, SurveyBounds.Min.Z), ECC_Visibility, SurveyQuery))
    {
        const FName Surface =
            IsWaterSample(Hit.ImpactPoint) ? FName(TEXT("water")) : ClassifySurveyHit(Hit);
        const FVector2D Center(Start);
        // Merge only immediately adjacent equal samples in the same row, never across a gap.
        if (!Snapshot.Regions.IsEmpty() && Column > 0 && Snapshot.Regions.Last().Surface == Surface
            && FMath::IsNearlyEqual(Snapshot.Regions.Last().Center.Y, Center.Y)
            && FMath::IsNearlyEqual(
                Snapshot.Regions.Last().Center.X + Snapshot.Regions.Last().Size.X * 0.5,
                Center.X - SurveyCellSize * 0.5,
                UE::Wanderer::MapScan::RegionMergeTolerance))
        {
            Snapshot.Regions.Last().Center.X += SurveyCellSize * 0.5;
            Snapshot.Regions.Last().Size.X += SurveyCellSize;
        }
        else
        {
            FWandererMapRegion& Region = Snapshot.Regions.AddDefaulted_GetRef();
            Region.Center = Center;
            Region.Size = FVector2D(SurveyCellSize);
            Region.Surface = Surface;
        }
    }
    ++NextSurveyCell;
    Snapshot.bSurveying = NextSurveyCell < SurveyDimensions.X * SurveyDimensions.Y;
}

FName UWandererMapComponent::ClassifySurveyHit(const FHitResult& InHit) const
{
    if (const AActor* Actor = InHit.GetActor(); Actor && Actor->ActorHasTag(TEXT("MapBuilding")))
    {
        return TEXT("building");
    }
    if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(InHit.GetComponent());
        Mesh && Mesh->GetStaticMesh())
    {
        const FString Name = Mesh->GetStaticMesh()->GetName();
        if (Name.StartsWith(TEXT("SM_Tunnel_")))
        {
            return TEXT("tunnel");
        }
        if (Name.Contains(TEXT("Building")) || Name.Contains(TEXT("Roof"))
            || Name.Contains(TEXT("Wall")) || Name.Contains(TEXT("Ceiling")))
        {
            return TEXT("building");
        }
        // Authored weather roofs also use plain cube geometry with a roof material.
        for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
        {
            const UMaterialInterface* Material = Mesh->GetMaterial(Index);
            if (Material && Material->GetName().Contains(TEXT("Roof")))
            {
                return TEXT("building");
            }
        }
    }
    const ICharacterSurfaceProvider* Surface = Cast<ICharacterSurfaceProvider>(GetOwner());
    return Surface ? Surface->ResolveSurfaceName(InHit) : FName(TEXT("ground"));
}

bool UWandererMapComponent::IsWaterSample(const FVector& InPosition) const
{
    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakWater : SurveyWater)
    {
        const UStaticMeshComponent* Water = WeakWater.Get();
        if (!Water
            || Water->GetComponentLocation().Z
                   < InPosition.Z - UE::Wanderer::MapScan::WaterHeightTolerance)
        {
            continue;
        }
        if (Water->ComponentHasTag(TEXT("Natural31Lake")))
        {
            if (UE::Wanderer::Contours::LakeRadius(InPosition,
                    UE::Wanderer::DemoSurface::LakeRadiusX,
                    UE::Wanderer::DemoSurface::LakeRadiusY)
                < UE::Wanderer::MapScan::LakeContourLimit)
            {
                return true;
            }
            continue;
        }
        if (Water->ComponentHasTag(TEXT("Natural30Lake")))
        {
            if (UE::Wanderer::Contours::LakeRadius(InPosition)
                < UE::Wanderer::MapScan::LakeContourLimit)
            {
                return true;
            }
            continue;
        }
        FVector Min, Max;
        Water->GetLocalBounds(Min, Max);
        const FVector Local = Water->GetComponentTransform().InverseTransformPosition(InPosition);
        if (Local.X <= Min.X || Local.X >= Max.X || Local.Y <= Min.Y || Local.Y >= Max.Y)
        {
            continue;
        }
        const FVector Extent = (Max - Min) * 0.5;
        if (Extent.X <= UE_SMALL_NUMBER || Extent.Y <= UE_SMALL_NUMBER)
        {
            continue;
        }
        if (Water->ComponentHasTag(TEXT("IWSRearPool")))
        {
            const double Width = FMath::Lerp(Max.X,
                Max.X
                    * (UE::Wanderer::MapScan::RearPoolNarrowWidth
                        / UE::Wanderer::MapScan::RearPoolWideWidth),
                (Local.Y - Min.Y) / (Max.Y - Min.Y));
            if (FMath::Abs(Local.X) < Width - UE::Wanderer::MapScan::PoolEdgeInset)
            {
                return true;
            }
            continue;
        }
        const FVector Center = (Min + Max) * 0.5;
        const double X = (Local.X - Center.X) / Extent.X;
        const double Y = Water->ComponentHasTag(TEXT("IWSFrontPool"))
                             ? (Local.Y - Min.Y) / (Max.Y - Min.Y)
                             : (Local.Y - Center.Y) / Extent.Y;
        if (X * X + Y * Y < UE::Wanderer::MapScan::PoolContourLimit)
        {
            return true;
        }
    }
    return false;
}
