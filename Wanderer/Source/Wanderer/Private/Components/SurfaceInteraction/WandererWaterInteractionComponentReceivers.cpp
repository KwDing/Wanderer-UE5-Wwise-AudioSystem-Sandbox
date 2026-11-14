#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UnrealType.h"

#include "Components/SurfaceInteraction/WandererInteractionBridge.h"
#include "Core/NaturalSurfaceUtils.h"
#include "Data/DemoSurfaceData.h"
#include "Data/SurfaceInteractionData.h"

void UWandererWaterInteractionComponent::HideSolverPlane(AActor& InSolver)
{
    // Solver may change its visibility on activation; only the original bounded receiver is drawn.
    FObjectPropertyBase* PlaneProperty =
        FindFProperty<FObjectPropertyBase>(InSolver.GetClass(), TEXT("WaterPlaneMesh"));
    UStaticMeshComponent* Plane =
        PlaneProperty ? Cast<UStaticMeshComponent>(
                            PlaneProperty->GetObjectPropertyValue_InContainer(&InSolver))
                      : nullptr;
    if (Plane)
    {
        Plane->SetHiddenInGame(true);
        Plane->SetVisibility(false);
    }
}

void UWandererWaterInteractionComponent::UpdateReceiverMotion(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter, bool bInIsWet)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    if (!Solver || !Surface)
    {
        return;
    }
    InOutReceiver.PendingFallSpeed =
        FMath::Max(InOutReceiver.PendingFallSpeed, -InOutCharacter.GetVelocity().Z);
    if (!bInIsWet && InOutCharacter.GetCharacterMovement()->IsMovingOnGround())
    {
        InOutReceiver.PendingFallSpeed = 0.0f;
    }
    const FVector Point(InOutCharacter.GetActorLocation().X,
        InOutCharacter.GetActorLocation().Y,
        Surface->GetComponentLocation().Z);
    if (bInIsWet && !InOutReceiver.bWasWet)
    {
        // Surface swimming cancels vertical velocity on entry; retain the descending sample.
        const float FallSpeed = FMath::Max(
            0.0f, -FMath::Min(InOutReceiver.PreviousVerticalSpeed, InOutCharacter.GetVelocity().Z));
        const float EntryMotion = FMath::Clamp(
            FallSpeed / UE::Wanderer::SurfaceInteraction::EntryReferenceSpeed, 0.0f, 1.0f);
        const float EntryStrength =
            FMath::Lerp(UE::Wanderer::SurfaceInteraction::EntryImpactMinStrength,
                UE::Wanderer::SurfaceInteraction::EntryImpactMaxStrength,
                EntryMotion);
        InOutReceiver.PendingFallSpeed = FMath::Max(InOutReceiver.PendingFallSpeed, FallSpeed);
        if (UWandererInteractionBridge::ApplyIWSWaterImpact(Solver,
                Point,
                EntryStrength,
                FMath::Lerp(UE::Wanderer::SurfaceInteraction::EntryImpactMinRadius,
                    UE::Wanderer::SurfaceInteraction::EntryImpactMaxRadius,
                    EntryMotion)))
        {
            ++WaterImpactCount;
        }
        if (!CanUpdate())
        {
            return;
        }
        InOutReceiver.LastImpact = Point;
        InOutReceiver.LastFootstep = Point;
    }
    if (bInIsWet && InOutReceiver.bWasWet
        && FVector::DistSquared2D(Point, InOutReceiver.LastImpact)
               >= FMath::Square(UE::Wanderer::SurfaceInteraction::ImpactTravelDistance))
    {
        const float Motion =
            FMath::Clamp(InOutCharacter.GetVelocity().Size2D()
                             / UE::Wanderer::SurfaceInteraction::MotionReferenceSpeed,
                0.0f,
                1.0f);
        if (UWandererInteractionBridge::ApplyIWSWaterImpact(Solver,
                Point,
                FMath::Lerp(UE::Wanderer::SurfaceInteraction::TravelImpactMinStrength,
                    UE::Wanderer::SurfaceInteraction::TravelImpactMaxStrength,
                    Motion),
                FMath::Lerp(UE::Wanderer::SurfaceInteraction::TravelImpactMinRadius,
                UE::Wanderer::SurfaceInteraction::TravelImpactMaxRadius,
                    Motion)))
        {
            ++WaterImpactCount;
        }
        if (!CanUpdate())
        {
            return;
        }
        InOutReceiver.LastImpact = Point;
    }
}

void UWandererWaterInteractionComponent::UpdateReceiverRegistration(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter, bool bInIsWet)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    if (!Solver)
    {
        return;
    }
    if (bInIsWet && !RegisteredWaterActors.Contains(Solver)
        && UWandererInteractionBridge::RegisterWaterCharacterMesh(Solver, &InOutCharacter))
    {
        RegisteredWaterActors.Add(Solver);
    }
    if (!CanUpdate())
    {
        return;
    }
    if (!bInIsWet && RegisteredWaterActors.Contains(Solver))
    {
        UWandererInteractionBridge::UnregisterWaterCharacterMesh(Solver, &InOutCharacter);
        RegisteredWaterActors.Remove(Solver);
        if (!CanUpdate())
        {
            return;
        }
    }
}

void UWandererWaterInteractionComponent::UpdateLegacyRegistrations(ACharacter& InOutCharacter)
{
    for (const TWeakObjectPtr<AActor>& Observed : WaterInteractionActors)
    {
        if (!CanUpdate())
        {
            return;
        }
        AActor* WaterActor = Observed.Get();
        if (!WaterActor || WaterActor->ActorHasTag(TEXT("TrialIWSWater")))
        {
            continue;
        }
        if (UWandererInteractionBridge::RefreshWaterCharacter(WaterActor, &InOutCharacter)
            && !RegisteredWaterActors.Contains(Observed)
            && UWandererInteractionBridge::RegisterWaterCharacterMesh(WaterActor, &InOutCharacter))
        {
            RegisteredWaterActors.Add(Observed);
        }
    }
}

void UWandererWaterInteractionComponent::FindIWSReceivers()
{
    for (const TWeakObjectPtr<AActor>& Observed : WaterInteractionActors)
    {
        AActor* Solver = Observed.Get();
        if (!Solver || !Solver->ActorHasTag(TEXT("TrialIWSWater")))
        {
            continue;
        }
        FString Label;
        for (const FName Tag : Solver->Tags)
        {
            const FString Text = Tag.ToString();
            if (Text.StartsWith(TEXT("IWSReceiver:")))
            {
                Label = Text.RightChop(12);
            }
        }
        if (Label.IsEmpty())
        {
            continue;
        }
        for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
        {
            if (!Actor->ActorHasTag(FName(*Label)))
            {
                continue;
            }
            UStaticMeshComponent* Surface = Actor->FindComponentByClass<UStaticMeshComponent>();
            if (!Surface)
            {
                continue;
            }
            UMaterialInterface* Template = Surface->GetMaterial(0);
            // Character replacement spawns the new pawn before unpossessing the old one.
            // Borrow its asset parent, never retain the outgoing pawn's simulation MID.
            if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Template))
            {
                Template = Existing->Parent;
            }
            if (!ensureMsgf(Template && !Cast<UMaterialInstanceDynamic>(Template),
                    TEXT("Trial water receiver must start from an authored material asset.")))
            {
                continue;
            }
            FWandererIWSReceiver& Receiver = IWSReceivers.AddDefaulted_GetRef();
            Receiver.SolverWeakPtr = Solver;
            Receiver.SurfaceWeakPtr = Surface;
            Receiver.SurfaceTemplate = Template;
            Receiver.Waves.Reserve(UE::Wanderer::SurfaceInteraction::MaximumContactWaves);
            for (const FName Bone : {FName(TEXT("hand_l")),
                     FName(TEXT("hand_r")),
                     FName(TEXT("foot_l")),
                     FName(TEXT("foot_r"))})
            {
                FWandererWaterContact& Contact = Receiver.Contacts.AddDefaulted_GetRef();
                Contact.Bone = Bone;
            }
            AddTickPrerequisiteActor(Solver);
            break;
        }
    }
}

void UWandererWaterInteractionComponent::UpdateIWSReceiver(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    if (!Solver || !Surface)
    {
        SuspendWaterReceiver(InOutReceiver, InOutCharacter);
        ReleaseSurfaceMaterial(InOutReceiver);
        return;
    }
    FObjectPropertyBase* Property =
        FindFProperty<FObjectPropertyBase>(Solver->GetClass(), TEXT("Dynamic Water Material"));
    UMaterialInstanceDynamic* Material =
        Property
            ? Cast<UMaterialInstanceDynamic>(Property->GetObjectPropertyValue_InContainer(Solver))
            : nullptr;
    if (!Material || !BindSurfaceMaterial(InOutReceiver, *Material))
    {
        SuspendWaterReceiver(InOutReceiver, InOutCharacter);
        return;
    }
    HideSolverPlane(*Solver);
    const bool bIsWet = IsInsideWater(*Surface, InOutCharacter);
    UpdateReceiverMotion(InOutReceiver, InOutCharacter, bIsWet);
    if (!CanUpdate())
    {
        return;
    }
    UpdateReceiverRegistration(InOutReceiver, InOutCharacter, bIsWet);
    if (!CanUpdate())
    {
        return;
    }
    InOutReceiver.bWasWet = bIsWet;
    InOutReceiver.PreviousVerticalSpeed = InOutCharacter.GetVelocity().Z;
    if (bIsWet)
    {
        UpdateBodyWaterline(InOutReceiver, InOutCharacter);
        if (!CanUpdate())
        {
            return;
        }
        UpdateWaterContacts(InOutReceiver, InOutCharacter);
        return;
    }
    if (!InOutReceiver.Waves.IsEmpty())
    {
        UpdateBodyWaterline(InOutReceiver, InOutCharacter);
    }
    else
    {
        ReleaseBodyWaterline(InOutReceiver);
    }
    for (FWandererWaterContact& Contact : InOutReceiver.Contacts)
    {
        Contact.bHasSample = false;
        Contact.StrokeDistance = 0.0f;
    }
}

void UWandererWaterInteractionComponent::SuspendWaterReceiver(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    if (Solver && RegisteredWaterActors.Contains(InOutReceiver.SolverWeakPtr))
    {
        UWandererInteractionBridge::UnregisterWaterCharacterMesh(Solver, &InOutCharacter);
    }
    RegisteredWaterActors.Remove(InOutReceiver.SolverWeakPtr);
    ReleaseBodyWaterline(InOutReceiver);
    InOutReceiver.bWasWet = false;
    InOutReceiver.PreviousVerticalSpeed = 0.0f;
    InOutReceiver.PendingFallSpeed = 0.0f;
    InOutReceiver.LastContactTime = UE::Wanderer::SurfaceInteraction::NoContactTime;
    for (FWandererWaterContact& Contact : InOutReceiver.Contacts)
    {
        Contact.bHasSample = false;
        Contact.StrokeDistance = 0.0f;
    }
}

bool UWandererWaterInteractionComponent::IsInsideWater(
    const UStaticMeshComponent& InSurface, const ACharacter& InCharacter) const
{
    const FVector Position = InCharacter.GetActorLocation();
    const float HalfHeight = InCharacter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float WaterHeight = InSurface.GetComponentLocation().Z;
    if (Position.Z - HalfHeight > WaterHeight + UE::Wanderer::SurfaceInteraction::WaterFootAllowance
        || Position.Z + HalfHeight < WaterHeight)
    {
        return false;
    }
    return IsOverWater(InSurface, Position);
}

bool UWandererWaterInteractionComponent::IsOverWater(
    const UStaticMeshComponent& InSurface, const FVector& InPosition) const
{
    if (InSurface.ComponentHasTag(TEXT("Natural31Lake")))
    {
        return UE::Wanderer::Contours::LakeRadius(InPosition,
                   UE::Wanderer::DemoSurface::LakeRadiusX,
                   UE::Wanderer::DemoSurface::LakeRadiusY)
               < UE::Wanderer::DemoSurface::LakeContainmentInset;
    }
    if (InSurface.ComponentHasTag(TEXT("Natural30Lake")))
    {
        return UE::Wanderer::Contours::LakeRadius(InPosition)
               < UE::Wanderer::DemoSurface::LakeContainmentInset;
    }
    const FVector Local = InSurface.GetComponentTransform().InverseTransformPosition(InPosition);
    FVector Min;
    FVector Max;
    InSurface.GetLocalBounds(Min, Max);
    if (Local.X <= Min.X || Local.X >= Max.X || Local.Y <= Min.Y || Local.Y >= Max.Y)
    {
        return false;
    }
    if (InSurface.ComponentHasTag(TEXT("IWSFrontPool")))
    {
        // Matches the authored half ellipse, not its rectangular bounds.
        const double X = Local.X / Max.X;
        const double Y = (Local.Y - Min.Y) / (Max.Y - Min.Y);
        return X * X + Y * Y < UE::Wanderer::DemoSurface::PoolContainmentInset;
    }
    if (InSurface.ComponentHasTag(TEXT("IWSRearPool")))
    {
        const double Fraction = (Local.Y - Min.Y) / (Max.Y - Min.Y);
        const double Width =
            FMath::Lerp(Max.X, Max.X * UE::Wanderer::DemoSurface::RearPoolWidthRatio, Fraction);
        return FMath::Abs(Local.X) < Width - UE::Wanderer::DemoSurface::RearPoolEdgeInset;
    }
    const FVector Center = (Min + Max) * 0.5;
    const FVector Extent = (Max - Min) * 0.5;
    const double X = (Local.X - Center.X) / Extent.X;
    const double Y = (Local.Y - Center.Y) / Extent.Y;
    return X * X + Y * Y < UE::Wanderer::DemoSurface::PoolContainmentInset;
}
