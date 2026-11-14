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

#include "Core/WandererEventBusSubsystem.h"
#include "Core/NaturalSurfaceUtils.h"
#include "Data/WandererPuddleMaskSettings.h"

UWandererSurfaceComponent::UWandererSurfaceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
}

TEnumAsByte<EPhysicalSurface> UWandererSurfaceComponent::GetCurrentPhysicalSurface() const
{
    for (const FPhysicalSurfaceName& Entry : GetDefault<UPhysicsSettings>()->PhysicalSurfaces)
    {
        if (Entry.Name == CurrentSurface)
        {
            return Entry.Type;
        }
    }
    return SurfaceType_Default;
}

FName UWandererSurfaceComponent::ResolveSurface(const FHitResult& Hit) const
{
    const EPhysicalSurface Type = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
    if (Type == SurfaceType_Default)
    {
        return TEXT("ground");
    }
    for (const FPhysicalSurfaceName& Entry : GetDefault<UPhysicsSettings>()->PhysicalSurfaces)
    {
        if (Entry.Type == Type)
        {
            return Entry.Name;
        }
    }
    return TEXT("ground");
}

FName UWandererSurfaceComponent::ResolveContactWetness(
    const FHitResult& Hit, const FVector& Feet) const
{
    if (!Hit.bBlockingHit)
    {
        return ResolvePuddleWetness(Feet);
    }
    const float WaterDepth = GetContactWaterDepth(Hit);
    if (WaterDepth > 0.0f)
    {
        return WaterDepth >= FMath::Max(0.1f, WetFootstepDepth) ? FName(TEXT("wet"))
                                                                : FName(TEXT("mild"));
    }
    // A damp bank mask is not evidence of shoe-deep standing water.
    if (const AActor* Ground = Hit.GetActor();
        Ground && Ground->ActorHasTag(TEXT("Natural31Terrain")))
    {
        const double Moisture = UE::Wanderer::Contours::LakeGroundMoisture(Hit.ImpactPoint);

        if (Moisture >= 0.15)
        {
            return TEXT("mild");
        }
    }
    return ResolvePuddleWetness(Hit.ImpactPoint);
}

void UWandererSurfaceComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshSurfaceRegions();
}

void UWandererSurfaceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bStopped = true;
    SetComponentTickEnabled(false);
    OnSurfaceChanged.Clear();
    OnWetnessChanged.Clear();
    Puddles.Reset();
    PuddleDecals.Reset();
    PoolWaterSurfaces.Reset();
    WaterVolumes.Reset();
    PuddleMasks.Reset();
    ObservedCharacter.Reset();
    Super::EndPlay(EndPlayReason);
}

void UWandererSurfaceComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    SampleSurface();
}
