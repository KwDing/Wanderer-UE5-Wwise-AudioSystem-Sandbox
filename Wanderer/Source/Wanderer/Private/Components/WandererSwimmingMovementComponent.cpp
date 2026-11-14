#include "Components/WandererSwimmingMovementComponent.h"

#include "Components/BrushComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"

bool UWandererSwimmingMovementComponent::GetSurfaceHeight(float& OutHeight) const
{
    const APhysicsVolume* Volume = GetPhysicsVolume();
    if (!Volume || !Volume->bWaterVolume || !Volume->ActorHasTag(TEXT("WaterBody")))
    {
        Volume = FindContainingWaterVolume();
    }
    if (!Volume || !Volume->bWaterVolume || !Volume->ActorHasTag(TEXT("WaterBody"))
        || !Volume->GetBrushComponent())
    {
        return false;
    }
    OutHeight = Volume->GetBrushComponent()->Bounds.GetBox().Max.Z;
    return true;
}

bool UWandererSwimmingMovementComponent::IsDiving() const
{
    return IsSwimming() && bDiving;
}

void UWandererSwimmingMovementComponent::PhysSwimming(float DeltaSeconds, int32 Iterations)
{
    float SurfaceHeight = 0.0f;
    const bool bHasSurface = GetSurfaceHeight(SurfaceHeight);
    if (Acceleration.Z < -1.0f)
    {
        bDiving = true;
    }
    if (!bHasSurface)
    {
        bDiving = false;
    }
    const bool bSurfaceSwimming = bHasSurface && !bDiving;
    const bool bStopWithoutInput = bHasSurface && Acceleration.IsNearlyZero();
    // Stop before integration so releasing input cannot produce another drifting step.
    if (bStopWithoutInput)
    {
        Velocity = FVector::ZeroVector;
    }
    const float SavedBuoyancy = Buoyancy;
    if (bHasSurface)
    {
        // Neutral buoyancy lets a submerged swimmer rest at depth instead of auto-surfacing.
        const float Depth = ImmersionDepth();
        if (Depth > UE_SMALL_NUMBER)
        {
            Buoyancy = 1.0f / Depth;
        }
    }
    if (bSurfaceSwimming)
    {
        Velocity.Z = 0.0f;
        if (Acceleration.SizeSquared2D() < 1.0f)
        {
            Velocity.X = 0.0f;
            Velocity.Y = 0.0f;
        }
    }
    Super::PhysSwimming(DeltaSeconds, Iterations);
    Buoyancy = SavedBuoyancy;
    if (bStopWithoutInput && IsSwimming())
    {
        Velocity = FVector::ZeroVector;
    }
    if (bHasSurface && IsSwimming() && bDiving)
    {
        FinishSurfaceAscent(SurfaceHeight);
    }
    if (bSurfaceSwimming && Acceleration.SizeSquared2D() < 1.0f)
    {
        Velocity.X = 0.0f;
        Velocity.Y = 0.0f;
    }
    if (bHasSurface && !bDiving && IsSwimming())
    {
        KeepHeadAboveSurface(SurfaceHeight, DeltaSeconds);
    }
    if (!IsSwimming())
    {
        bDiving = false;
    }
}

void UWandererSwimmingMovementComponent::OnMovementModeChanged(
    EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
    if (!IsSwimming())
    {
        bDiving = false;
    }
}

const APhysicsVolume* UWandererSwimmingMovementComponent::FindContainingWaterVolume() const
{
    if (!IsSwimming())
    {
        return nullptr;
    }
    // The movement component can report the default volume briefly during a water transition.
    UWorld* World = GetWorld();
    if (!World || !UpdatedComponent)
    {
        return nullptr;
    }
    for (TActorIterator<APhysicsVolume> Candidate(World); Candidate; ++Candidate)
    {
        if (Candidate->bWaterVolume && Candidate->ActorHasTag(TEXT("WaterBody"))
            && Candidate->EncompassesPoint(UpdatedComponent->GetComponentLocation()))
        {
            return *Candidate;
        }
    }
    return nullptr;
}

float UWandererSwimmingMovementComponent::GetSurfaceTargetHeight(float SurfaceHeight) const
{
    const USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
    if (!Mesh || !UpdatedComponent || !Mesh->DoesSocketExist(TEXT("head")))
    {
        return SurfaceHeight - 8.0f;
    }
    const float HeadHeight = Mesh->GetSocketLocation(TEXT("head")).Z;
    const float CurrentHeight = UpdatedComponent->GetComponentLocation().Z;
    const float DesiredClearance =
        Acceleration.SizeSquared2D() >= 1.0f ? ActiveSwimmingHeadClearance : HeadClearance;
    return FMath::Min(
        CurrentHeight + SurfaceHeight + DesiredClearance - HeadHeight, SurfaceHeight - 8.0f);
}

void UWandererSwimmingMovementComponent::KeepHeadAboveSurface(
    float SurfaceHeight, float DeltaSeconds)
{
    if (!UpdatedComponent)
    {
        return;
    }
    const float CurrentHeight = UpdatedComponent->GetComponentLocation().Z;
    const float TargetHeight = GetSurfaceTargetHeight(SurfaceHeight);
    // Clamp the interpolation fraction, not just speed: background frames must not
    // overshoot the water boundary and make PhysicsVolumeChanged leave/re-enter it.
    const float InterpolatedHeight =
        FMath::FInterpTo(CurrentHeight, TargetHeight, DeltaSeconds, 8.0f);
    const float Change = FMath::Clamp(
        InterpolatedHeight - CurrentHeight, -90.0f * DeltaSeconds, 300.0f * DeltaSeconds);
    FHitResult Hit;
    SafeMoveUpdatedComponent(
        FVector(0.0f, 0.0f, Change), UpdatedComponent->GetComponentQuat(), true, Hit);
    Velocity.Z = 0.0f;
}

void UWandererSwimmingMovementComponent::FinishSurfaceAscent(float SurfaceHeight)
{
    const float Ceiling = GetSurfaceTargetHeight(SurfaceHeight);
    const float Excess = UpdatedComponent->GetComponentLocation().Z - Ceiling;
    if (Excess >= 0.0f && Acceleration.Z >= 0.0f)
    {
        FHitResult Hit;
        SafeMoveUpdatedComponent(
            FVector(0, 0, -Excess), UpdatedComponent->GetComponentQuat(), true, Hit);
        Velocity.Z = FMath::Min(0.0f, Velocity.Z);
        bDiving = false;
    }
}
