#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

#include "Components/SurfaceInteraction/WandererInteractionBridge.h"
#include "Core/WaterContactUtils.h"
#include "Data/SurfaceInteractionData.h"

void UWandererWaterInteractionComponent::EmitEntrySplash(FWandererIWSReceiver& InOutReceiver)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    if (!Solver || !Surface || InOutReceiver.BodySections.IsEmpty())
    {
        return;
    }
    InOutReceiver.EntryEnergy = FMath::Clamp(
        InOutReceiver.PendingFallSpeed / UE::Wanderer::SurfaceInteraction::EntryReferenceSpeed,
        UE::Wanderer::SurfaceInteraction::MinimumEntryEnergy,
        1.0f);
    const FBox ContactBounds = UE::Wanderer::Water::GetSectionBounds(InOutReceiver.BodySections);
    FVector Center = ContactBounds.GetCenter();
    Center.Z = Surface->GetComponentLocation().Z;
    AddWaterWave(InOutReceiver,
        Center,
        InOutReceiver.EntryEnergy,
        FMath::Clamp(ContactBounds.GetExtent().Size2D() * 0.5f,
            UE::Wanderer::SurfaceInteraction::MinimumEntryWaveRadius,
            UE::Wanderer::SurfaceInteraction::MaximumEntryWaveRadius));
    bool bEmittedSpray = false;
    for (int32 Side = 0; Side < UE::Wanderer::SurfaceInteraction::EntrySprayDirections; ++Side)
    {
        const float Angle = Side * HALF_PI;
        const FVector Point = Center
                              + FVector(FMath::Cos(Angle) * ContactBounds.GetExtent().X,
                                  FMath::Sin(Angle) * ContactBounds.GetExtent().Y,
                                  0.0f);
        const FVector Edge =
            UE::Wanderer::Water::ProjectToBodyEdge(Point, InOutReceiver.BodySections);
        if (!CanUpdate() || !IsValid(Solver) || Solver->IsActorBeingDestroyed()
            || !IsValid(Surface))
        {
            return;
        }
        if (IsOverWater(*Surface, Edge))
        {
            bEmittedSpray |= CreateWaterSpray(*Solver, Edge, InOutReceiver.EntryEnergy);
        }
    }
    if (bEmittedSpray)
    {
        ++EntrySplashCount;
    }
}

bool UWandererWaterInteractionComponent::HasSplashDepth(
    AActor& InOutSolver, const FVector& InLocation) const
{
    // Capsule overlap includes the last thin shoreline film. A crown needs water
    // above the actual solid surface, rather than spraying through exposed sand.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WaterContactDepth), true, GetOwner());
    Query.AddIgnoredActor(&InOutSolver);
    FHitResult Hit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,
        InLocation + FVector(0.0f, 0.0f, UE::Wanderer::SurfaceInteraction::MinimumSplashDepth),
        InLocation
            - FVector(0.0f, 0.0f, UE::Wanderer::SurfaceInteraction::SplashDepthTraceDistance),
        ECC_Visibility,
        Query);
    return !bHit
           || Hit.ImpactPoint.Z
                  < InLocation.Z - UE::Wanderer::SurfaceInteraction::MinimumSplashDepth;
}

bool UWandererWaterInteractionComponent::CreateWaterSpray(AActor& InOutSolver,
    const FVector& InLocation,
    float InPower,
    FName InContactBone,
    UStaticMeshComponent* InContactSurface)
{
    UNiagaraSystem* Template =
        InContactBone.IsNone() ? SprayTemplate.Get() : SwimmingSprayTemplate.Get();
    if (!Template || (!InContactBone.IsNone() && !IsValid(InContactSurface))
        || Splashes.Num() >= UE::Wanderer::SurfaceInteraction::MaximumSprays
        || !IsValid(&InOutSolver) || InOutSolver.GetWorld() != GetWorld()
        || InLocation.ContainsNaN() || !FMath::IsFinite(InPower)
        || !HasSplashDepth(InOutSolver, InLocation))
    {
        return false;
    }
    UNiagaraComponent* Spray = NewObject<UNiagaraComponent>(&InOutSolver, NAME_None, RF_Transient);
    Spray->SetAutoActivate(false);
    Spray->SetAutoDestroy(false);
    Spray->SetAsset(Template);
    Spray->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Spray->SetCastShadow(false);
    Spray->SetCanEverAffectNavigation(false);
    Spray->SetWorldLocation(
        InLocation + FVector(0.0f, 0.0f, UE::Wanderer::SurfaceInteraction::SprayHeightOffset));
    // Supplier amount scales its burst; it is not a direct particle count.
    Spray->SetVariableFloat(TEXT("User.Splash Amount"),
        FMath::Lerp(UE::Wanderer::SurfaceInteraction::SprayMinAmount,
            UE::Wanderer::SurfaceInteraction::SprayMaxAmount,
            FMath::Clamp(InPower, 0.0f, 1.0f)));
    Spray->SetVariableFloat(TEXT("User.Splash Power"),
        FMath::Lerp(UE::Wanderer::SurfaceInteraction::SprayMinPower,
            UE::Wanderer::SurfaceInteraction::SprayMaxPower,
            FMath::Clamp(InPower, 0.0f, 1.0f)));
    Spray->SetVariableFloat(TEXT("User.Splash Radius"),
        FMath::Lerp(UE::Wanderer::SurfaceInteraction::SprayMinRadius,
            UE::Wanderer::SurfaceInteraction::SprayMaxRadius,
            FMath::Clamp(InPower, 0.0f, 1.0f)));
    Spray->RegisterComponent();
    if (!CanUpdate() || !Spray->IsRegistered())
    {
        Spray->DestroyComponent();
        return false;
    }
    FWandererIWSSplash& Splash = Splashes.AddDefaulted_GetRef();
    Splash.ComponentWeakPtr = Spray;
    Splash.ContactBone = InContactBone;
    Splash.ContactSurfaceWeakPtr = InContactSurface;
    Splash.StartTime = GetWorld()->GetTimeSeconds();
    // Detached droplets settle quickly; the live body contour carries the ongoing
    // contact response instead of leaving a stationary plume behind a fast swimmer.
    Splash.Expiry =
        Splash.StartTime
        + (InContactBone.IsNone() ? UE::Wanderer::SurfaceInteraction::EntrySprayLifetime
                                  : UE::Wanderer::SurfaceInteraction::SwimmingSprayLifetime);
    Spray->Activate(true);
    return true;
}

bool UWandererWaterInteractionComponent::UpdateSwimmingSpray(FWandererIWSSplash& InOutSplash)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UStaticMeshComponent* Surface = InOutSplash.ContactSurfaceWeakPtr.Get();
    UNiagaraComponent* Spray = Cast<UNiagaraComponent>(InOutSplash.ComponentWeakPtr.Get());
    if (!Character || !Surface || !Spray || !Character->GetCharacterMovement()->IsSwimming()
        || Character->GetVelocity().Size2D() < UE::Wanderer::SurfaceInteraction::MinimumSpraySpeed
        || !Character->GetMesh()->DoesSocketExist(InOutSplash.ContactBone))
    {
        return false;
    }
    FVector Contact = Character->GetMesh()->GetSocketLocation(InOutSplash.ContactBone);
    const float Height = Surface->GetComponentLocation().Z;
    if (FMath::Abs(Contact.Z - Height) > UE::Wanderer::SurfaceInteraction::SprayFollowBand
        || !IsOverWater(*Surface, Contact))
    {
        return false;
    }
    Contact.Z = Height + UE::Wanderer::SurfaceInteraction::SprayHeightOffset;
    if (FVector::DistSquared(Contact, Spray->GetComponentLocation())
        > FMath::Square(UE::Wanderer::SurfaceInteraction::MaximumSprayFollowDistance))
    {
        return false;
    }
    // The dedicated emitter simulates in local space, so existing contact droplets
    // move too. Moving only a world-space emitter would leave its burst behind.
    Spray->SetWorldLocation(Contact);
    return true;
}

void UWandererWaterInteractionComponent::UpdateWaterContacts(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter)
{
    InOutReceiver.AnimatedMotion = 0.0f;
    for (FWandererWaterContact& Contact : InOutReceiver.Contacts)
    {
        if (!CanUpdate())
        {
            return;
        }
        UpdateBoneContact(InOutReceiver, Contact, InOutCharacter);
    }
}

void UWandererWaterInteractionComponent::EmitWaterContact(
    FWandererIWSReceiver& InOutReceiver, const FVector& InLocation, float InSpeed, FName InBone)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    const float Now = GetWorld()->GetTimeSeconds();
    if (!Solver || !Surface
        || Now - InOutReceiver.LastContactTime < UE::Wanderer::SurfaceInteraction::ContactInterval
        || !IsOverWater(*Surface, InLocation))
    {
        return;
    }
    InOutReceiver.LastContactTime = Now;
    const float Motion =
        FMath::Clamp(InSpeed / UE::Wanderer::SurfaceInteraction::MotionReferenceSpeed, 0.0f, 1.0f);
    const FVector Edge =
        UE::Wanderer::Water::ProjectToBodyEdge(InLocation, InOutReceiver.BodySections);
    if (!IsOverWater(*Surface, Edge))
    {
        return;
    }
    AddWaterWave(InOutReceiver,
        Edge,
        FMath::Lerp(UE::Wanderer::SurfaceInteraction::ContactWaveMinStrength,
            UE::Wanderer::SurfaceInteraction::ContactWaveMaxStrength,
            Motion),
        UE::Wanderer::SurfaceInteraction::ContactWaveRadius);
    if (UWandererInteractionBridge::ApplyIWSWaterImpact(Solver,
            Edge,
            FMath::Lerp(UE::Wanderer::SurfaceInteraction::ContactImpactMinStrength,
                UE::Wanderer::SurfaceInteraction::ContactImpactMaxStrength,
                Motion),
            FMath::Lerp(UE::Wanderer::SurfaceInteraction::ContactImpactMinRadius,
                UE::Wanderer::SurfaceInteraction::ContactImpactMaxRadius,
                Motion)))
    {
        ++WaterImpactCount;
    }
    if (!CanUpdate())
    {
        return;
    }
    // Normal swimming is about 115cm/s. Only faster contact throws a little spray.
    if (InSpeed < UE::Wanderer::SurfaceInteraction::MinimumSpraySpeed)
    {
        return;
    }
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const bool bSwimming = Character && Character->GetCharacterMovement()->IsSwimming();
    if (bSwimming && InBone != TEXT("hand_l") && InBone != TEXT("hand_r"))
    {
        return;
    }
    if (!InOutReceiver.BodySections.IsEmpty()
        && CreateWaterSpray(*Solver,
            bSwimming ? InLocation : Edge,
            FMath::Lerp(UE::Wanderer::SurfaceInteraction::ContactSprayMinPower,
                UE::Wanderer::SurfaceInteraction::ContactSprayMaxPower,
                Motion),
            bSwimming ? InBone : NAME_None,
            bSwimming ? Surface : nullptr))
    {
        ++ContactSplashCount;
    }
}

void UWandererWaterInteractionComponent::UpdateBoneContact(FWandererIWSReceiver& InOutReceiver,
    FWandererWaterContact& InOutContact,
    ACharacter& InOutCharacter)
{
    USkeletalMeshComponent* Mesh = InOutCharacter.GetMesh();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    if (!Mesh || !Surface || !Mesh->DoesSocketExist(InOutContact.Bone))
    {
        return;
    }
    const float Height = Surface->GetComponentLocation().Z;
    const float Now = GetWorld()->GetTimeSeconds();
    const float DeltaTime = FMath::Max(
        GetWorld()->GetDeltaSeconds(), UE::Wanderer::SurfaceInteraction::MinimumSampleTime);
    const FVector Position = Mesh->GetSocketLocation(InOutContact.Bone);
    const FVector RelativePosition =
        Mesh->GetComponentTransform().InverseTransformPosition(Position);
    const FVector RelativeTravel = InOutContact.bHasSample
                                       ? RelativePosition - InOutContact.PreviousRelativePosition
                                       : FVector::ZeroVector;
    const float StrokeTravel = RelativeTravel.Size();
    const float VerticalSpeed = RelativeTravel.Z / DeltaTime;
    const bool bNearSurface =
        FMath::Abs(Position.Z - Height) < UE::Wanderer::SurfaceInteraction::WaterContactBand;
    const bool bSwimming = InOutCharacter.GetCharacterMovement()->IsSwimming();
    const bool bFoot = InOutContact.Bone == TEXT("foot_l") || InOutContact.Bone == TEXT("foot_r");
    const float FeetHeight = InOutCharacter.GetActorLocation().Z
                             - InOutCharacter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (bNearSurface)
    {
        InOutReceiver.AnimatedMotion = FMath::Max(InOutReceiver.AnimatedMotion,
            FMath::Clamp(
                StrokeTravel
                    / (DeltaTime * UE::Wanderer::SurfaceInteraction::AnimatedReferenceSpeed),
                0.0f,
                UE::Wanderer::SurfaceInteraction::MaximumAnimatedMotion));
    }
    InOutContact.StrokeDistance = bNearSurface ? InOutContact.StrokeDistance + StrokeTravel : 0.0f;
    const bool bCrossedSurface =
        InOutContact.PreviousPosition.Z
            > Height + UE::Wanderer::SurfaceInteraction::SurfaceCrossingHeight
        && Position.Z <= Height + UE::Wanderer::SurfaceInteraction::SurfaceCrossingHeight;
    const bool bFootPlanted =
        bFoot && InOutCharacter.GetCharacterMovement()->IsMovingOnGround()
        && Position.Z < Height + UE::Wanderer::SurfaceInteraction::SurfaceCrossingHeight
        && Position.Z < FeetHeight + UE::Wanderer::SurfaceInteraction::FootPlantHeight
        && InOutContact.PreviousRelativeVerticalSpeed
               < UE::Wanderer::SurfaceInteraction::FootDescendingSpeed
        && VerticalSpeed >= UE::Wanderer::SurfaceInteraction::FootSettledSpeed;
    const bool bSkimmingStroke =
        bSwimming && bNearSurface
        && StrokeTravel > UE::Wanderer::SurfaceInteraction::MinimumStrokeTravel
        && InOutContact.StrokeDistance > UE::Wanderer::SurfaceInteraction::StrokeDistance;
    if (InOutContact.bHasSample && (bCrossedSurface || bFootPlanted || bSkimmingStroke)
        && Now - InOutContact.LastStrikeTime > UE::Wanderer::SurfaceInteraction::StrikeInterval)
    {
        const float Speed = FMath::Max(InOutCharacter.GetVelocity().Size2D(),
            bCrossedSurface ? FMath::Max(0.0f, -VerticalSpeed) : 0.0f);
        EmitWaterContact(
            InOutReceiver, FVector(Position.X, Position.Y, Height), Speed, InOutContact.Bone);
        InOutContact.LastStrikeTime = Now;
        InOutContact.StrokeDistance = 0.0f;
    }
    InOutContact.PreviousPosition = Position;
    InOutContact.PreviousRelativePosition = RelativePosition;
    InOutContact.PreviousRelativeVerticalSpeed = VerticalSpeed;
    InOutContact.bHasSample = true;
}

void UWandererWaterInteractionComponent::PruneSplashes(bool bInClearAll)
{
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    for (int32 Index = Splashes.Num() - 1; Index >= 0; --Index)
    {
        UActorComponent* Component = Splashes[Index].ComponentWeakPtr.Get();
        if (!bInClearAll && Component && Now < Splashes[Index].Expiry
            && (Splashes[Index].ContactBone.IsNone() || UpdateSwimmingSpray(Splashes[Index])))
        {
            continue;
        }
        Splashes.RemoveAtSwap(Index);
        if (Component)
        {
            Component->DestroyComponent();
        }
    }
}
