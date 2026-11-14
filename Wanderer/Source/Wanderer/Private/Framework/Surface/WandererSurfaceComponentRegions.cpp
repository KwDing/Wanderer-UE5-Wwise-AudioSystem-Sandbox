#include "Framework/Surface/WandererSurfaceComponent.h"

#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Templates/UnrealTemplate.h"

#include "Core/NaturalSurfaceUtils.h"
#include "Core/WandererEventBusSubsystem.h"
#include "Data/WandererPuddleMaskSettings.h"
#include "Data/WorldPresentationSettings.h"

namespace
{
    bool IsPoolSurface(const UStaticMeshComponent& InComponent,
        const UWorldPresentationSettings& InSettings, bool bInLegacy)
    {
        if (InComponent.ComponentHasTag(InSettings.WaterSurfaceTag))
        {
            return true;
        }
        if (!bInLegacy || InSettings.PoolMaterialPrefix.IsEmpty())
        {
            return false;
        }
        for (int32 Index = 0; Index < InComponent.GetNumMaterials(); ++Index)
        {
            const UMaterialInterface* Material = InComponent.GetMaterial(Index);
            if (Material && Material->GetName().StartsWith(InSettings.PoolMaterialPrefix))
            {
                return true;
            }
        }
        return false;
    }
}

void UWandererSurfaceComponent::RefreshSurfaceRegions()
{
    if (bStopped)
    {
        return;
    }
    RefreshPuddleMasks();
    Puddles.Reset();
    PuddleDecals.Reset();
    PoolWaterSurfaces.Reset();
    WaterVolumes.Reset();
    if (!GetWorld())
    {
        return;
    }
    const UWorldPresentationSettings* Settings = GetDefault<UWorldPresentationSettings>();
    const bool bLegacy = UWorldPresentationSettings::MatchesMap(
        GetWorld()->GetMapName(), Settings->LegacySurfaceMaps);
    for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
    {
        if (APhysicsVolume* Volume = Cast<APhysicsVolume>(*Actor); Volume && Volume->bWaterVolume)
        {
            WaterVolumes.Add(Volume);
        }
        TInlineComponentArray<UStaticMeshComponent*> Components(*Actor);
        for (UStaticMeshComponent* Component : Components)
        {
            if (Component->ComponentHasTag(Settings->PuddleSurfaceTag)
                || (bLegacy && !Settings->PuddleNameToken.IsEmpty() && Component->GetStaticMesh()
                    && Component->GetStaticMesh()->GetName().Contains(Settings->PuddleNameToken)))
            {
                Puddles.Add(Component);
            }
            if (IsPoolSurface(*Component, *Settings, bLegacy))
            {
                PoolWaterSurfaces.AddUnique(Component);
            }
        }
        TInlineComponentArray<UDecalComponent*> Decals(*Actor);
        for (UDecalComponent* Decal : Decals)
        {
            if (Decal->ComponentHasTag(Settings->PuddleSurfaceTag)
                || (bLegacy && !Settings->PuddleNameToken.IsEmpty() && Decal->GetDecalMaterial()
                    && Decal->GetDecalMaterial()->GetName().Contains(Settings->PuddleNameToken)))
            {
                PuddleDecals.Add(Decal);
            }
        }
    }
}

float UWandererSurfaceComponent::GetContactWaterDepth(const FHitResult& Hit) const
{
    if (!Hit.bBlockingHit)
    {
        return 0.0f;
    }
    const FVector Ground = Hit.ImpactPoint;
    float WaterDepth = GetPoolWaterDepth(Ground);
    for (const TWeakObjectPtr<APhysicsVolume>& WeakVolume : WaterVolumes)
    {
        const APhysicsVolume* Volume = WeakVolume.Get();
        const UBrushComponent* Brush = Volume ? Volume->GetBrushComponent() : nullptr;
        if (!Volume || !Volume->bWaterVolume || !Brush)
        {
            continue;
        }
        const double Depth = Brush->Bounds.GetBox().Max.Z - Ground.Z;
        if (Depth <= 0.0)
        {
            continue;
        }
        // Test the contact's XY inside the actual brush, just above the support.
        // This avoids borrowing the character center's water state on a shoreline.
        const FVector WaterPoint = Ground + FVector(0.0, 0.0, FMath::Min(Depth * 0.5, 0.5));
        if (Volume->EncompassesPoint(WaterPoint))
        {
            WaterDepth = FMath::Max(WaterDepth, static_cast<float>(Depth));
        }
    }
    return WaterDepth;
}

FName UWandererSurfaceComponent::ResolvePuddleWetness(const FVector& Ground) const
{
    FName Wetness = TEXT("dry");
    for (const TWeakObjectPtr<UDecalComponent>& WeakDecal : PuddleDecals)
    {
        const UDecalComponent* Decal = WeakDecal.Get();
        if (!Decal || !Decal->IsVisible())
        {
            continue;
        }
        const FVector Point = Decal->GetComponentTransform().InverseTransformPosition(Ground);
        const FVector Size = Decal->DecalSize;
        if (Size.Y <= 0 || Size.Z <= 0)
        {
            continue;
        }
        const double Y = Point.Y / (Size.Y * 0.65);
        const double Z = Point.Z / (Size.Z * 0.65);
        // UE 5.5 DeferredDecal.usf maps texture UV using local Z, Y (not Y, Z).
        const FVector2D UV(Point.Z / (2 * Size.Z) + 0.5, Point.Y / (2 * Size.Y) + 0.5);
        if (FMath::Abs(Point.X) <= Size.X
            && IsPuddleCore(Decal->GetDecalMaterial(), UV, Y * Y + Z * Z <= 1.0))
        {
            Wetness = TEXT("mild");
        }
    }
    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakPuddle : Puddles)
    {
        const UStaticMeshComponent* Puddle = WeakPuddle.Get();
        if (!Puddle || !Puddle->IsVisible())
        {
            continue;
        }
        FVector Min;
        FVector Max;
        Puddle->GetLocalBounds(Min, Max);
        const FVector Point = Puddle->GetComponentTransform().InverseTransformPosition(Ground);
        const FVector Center = (Min + Max) * 0.5;
        const FVector Extent = (Max - Min) * 0.5;
        if (Extent.X < UE_SMALL_NUMBER || Extent.Y < UE_SMALL_NUMBER)
        {
            continue;
        }
        const double HeightDistance = FMath::Abs(
            FVector::DotProduct(Ground - Puddle->GetComponentTransform().TransformPosition(Center),
                Puddle->GetUpVector()));
        // The visual plane has transparent corners. Use an inset ellipse instead of its full
        // bounding box.
        constexpr double PuddleFootprintRatio = 0.85;
        const double X = (Point.X - Center.X) / (Extent.X * PuddleFootprintRatio);
        const double Y = (Point.Y - Center.Y) / (Extent.Y * PuddleFootprintRatio);
        // Match the actual foot-support plane, not an overlapping puddle beneath a raised pavement.
        constexpr double PuddleSupportToleranceCm = 2.0;
        if (HeightDistance <= PuddleSupportToleranceCm && X * X + Y * Y <= 1.0)
        {
            Wetness = TEXT("mild");
        }
    }
    return Wetness;
}

void UWandererSurfaceComponent::RefreshPuddleMasks()
{
    PuddleMasks.Reset();
    for (const FWandererPuddleMask& Config : GetDefault<UWandererPuddleMaskSettings>()->Masks)
    {
        if (Config.Width <= 0 || Config.Height <= 0 || Config.Width > 256 || Config.Height > 256
            || Config.SampleHex.Len() != Config.Width * Config.Height * 2)
        {
            UE_LOG(
                LogTemp, Warning, TEXT("Ignoring malformed puddle mask dimensions/sample data."));
            continue;
        }
        FWandererCachedPuddleMask Mask;
        Mask.MaterialPath = Config.MaterialPath;
        Mask.Width = Config.Width;
        Mask.Height = Config.Height;
        Mask.CoreThreshold = FMath::Clamp(Config.CoreThreshold, 0.0f, 1.0f);
        Mask.Samples.SetNumUninitialized(Config.Width * Config.Height);
        if (!FString::ToHexBlob(Config.SampleHex, Mask.Samples.GetData(), Mask.Samples.Num()))
        {
            UE_LOG(
                LogTemp, Warning, TEXT("Ignoring puddle mask with invalid hexadecimal samples."));
            continue;
        }
        PuddleMasks.Add(MoveTemp(Mask));
    }
}

bool UWandererSurfaceComponent::IsPuddleCore(
    const UMaterialInterface* Material, const FVector2D& UV, bool bFallback) const
{
    if (!Material || UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1)
    {
        return false;
    }
    const UMaterial* BaseMaterial = Material->GetMaterial();
    if (!BaseMaterial)
    {
        return bFallback;
    }
    const FString MaterialPath = BaseMaterial->GetPathName();
    for (const FWandererCachedPuddleMask& Mask : PuddleMasks)
    {
        if (MaterialPath != Mask.MaterialPath)
        {
            continue;
        }
        const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * Mask.Width), 0, Mask.Width - 1);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * Mask.Height), 0, Mask.Height - 1);
        return Mask.Samples[Y * Mask.Width + X] / 255.0f >= Mask.CoreThreshold;
    }
    return bFallback;
}

float UWandererSurfaceComponent::GetPoolWaterDepth(const FVector& Ground) const
{
    float WaterDepth = 0.0f;
    const FName WaterSurfaceTag = GetDefault<UWorldPresentationSettings>()->WaterSurfaceTag;
    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakWater : PoolWaterSurfaces)
    {
        const UStaticMeshComponent* Water = WeakWater.Get();
        // Authored water bodies use exact brush containment; legacy Paris meshes retain the
        // approximation.
        if (!Water || Water->ComponentHasTag(WaterSurfaceTag) || !Water->IsVisible()
            || Water->GetUpVector().Z < 0.99)
        {
            continue;
        }
        const double Depth = Water->GetComponentLocation().Z - Ground.Z;
        // Rome's shallow fountains: feet must be submerged, not merely above the water's XY
        // footprint.
        constexpr double MaximumShallowDepth = 150.0;
        if (Depth <= 0.0 || Depth > MaximumShallowDepth)
        {
            continue;
        }
        FVector Min;
        FVector Max;
        Water->GetLocalBounds(Min, Max);
        const FVector Point = Water->GetComponentTransform().InverseTransformPosition(Ground);
        const FVector Center = (Min + Max) * 0.5;
        const FVector Extent = (Max - Min) * 0.5;
        if (Extent.X <= UE_SMALL_NUMBER || Extent.Y <= UE_SMALL_NUMBER)
        {
            continue;
        }
        const double X = (Point.X - Center.X) / (Extent.X * 0.95);
        const double Y = (Point.Y - Center.Y) / (Extent.Y * 0.95);
        if (X * X + Y * Y <= 1.0)
        {
            WaterDepth = FMath::Max(WaterDepth, static_cast<float>(Depth));
        }
    }
    return WaterDepth;
}
