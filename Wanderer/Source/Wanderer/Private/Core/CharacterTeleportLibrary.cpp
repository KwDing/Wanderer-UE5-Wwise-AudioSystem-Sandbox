#include "Core/CharacterTeleportLibrary.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

namespace
{
    constexpr double FloorProbeDistanceCm = 250.0;
    constexpr double FloorHeightToleranceCm = 100.0;
    constexpr double CapsuleFloorClearanceCm = 2.0;

    bool IsLiveCharacter(const ACharacter* InCharacter, const UWorld* InWorld)
    {
        return IsValid(InCharacter) && IsValid(InWorld) && !InWorld->bIsTearingDown
               && InCharacter->GetWorld() == InWorld && !InCharacter->IsActorBeingDestroyed();
    }

    bool FindDestination(ACharacter& InCharacter,
        const FVector& InGroundLocation,
        FVector& OutCenter,
        FText& OutFailure)
    {
        UCapsuleComponent* Capsule = InCharacter.GetCapsuleComponent();
        UCharacterMovementComponent* Movement = InCharacter.GetCharacterMovement();
        if (!IsValid(Capsule) || !IsValid(Movement))
        {
            OutFailure =
                FText::FromString(TEXT("This character has no supported movement capsule."));
            return false;
        }
        UWorld* World = InCharacter.GetWorld();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(WandererTeleport), false, &InCharacter);
        const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
        const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
        FHitResult Floor;
        const FVector Offset(0, 0, FloorProbeDistanceCm);
        if (!World->LineTraceSingleByChannel(Floor,
                InGroundLocation + Offset,
                InGroundLocation - Offset,
                Channel,
                Params,
                Responses)
            || !Movement->IsWalkable(Floor)
            || FMath::Abs(Floor.ImpactPoint.Z - InGroundLocation.Z) > FloorHeightToleranceCm)
        {
            OutFailure = FText::FromString(TEXT("No walkable ground at this destination."));
            return false;
        }
        OutCenter =
            Floor.ImpactPoint
            + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + CapsuleFloorClearanceCm);
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(
            Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
        if (World->OverlapBlockingTestByChannel(
                OutCenter, FQuat::Identity, Channel, Shape, Params, Responses))
        {
            OutFailure = FText::FromString(
                TEXT("The destination is blocked. Your position has not changed."));
            return false;
        }
        return true;
    }

    struct FPlacementObservation
    {
        TWeakObjectPtr<ACharacter> CharacterWeakPtr;
        TWeakObjectPtr<UWorld> WorldWeakPtr;
        TWeakObjectPtr<UCharacterMovementComponent> MovementWeakPtr;
        TWeakObjectPtr<AController> ControllerWeakPtr;
        bool bWasPlaying = false;

        explicit FPlacementObservation(ACharacter& InCharacter)
            : CharacterWeakPtr(&InCharacter), WorldWeakPtr(InCharacter.GetWorld()),
              MovementWeakPtr(InCharacter.GetCharacterMovement()),
              ControllerWeakPtr(InCharacter.GetController()),
              bWasPlaying(InCharacter.HasActorBegunPlay())
        {
        }

        bool IsCurrent() const
        {
            ACharacter* Character = CharacterWeakPtr.Get();
            return IsLiveCharacter(Character, WorldWeakPtr.Get())
                   && (!bWasPlaying || Character->HasActorBegunPlay());
        }
    };

    void FinishPlacement(const FPlacementObservation& InObservation, const FRotator& InArrival)
    {
        if (!InObservation.IsCurrent())
        {
            return;
        }
        ACharacter* Character = InObservation.CharacterWeakPtr.Get();
        UCharacterMovementComponent* Movement = InObservation.MovementWeakPtr.Get();
        if (IsValid(Movement) && !Movement->IsBeingDestroyed()
            && Character->GetCharacterMovement() == Movement)
        {
            Movement->StopMovementImmediately();
        }
        // Stop may end play or replace possession; re-observe before touching the controller.
        Character = InObservation.CharacterWeakPtr.Get();
        AController* Controller = InObservation.ControllerWeakPtr.Get();
        if (!InObservation.IsCurrent() || !IsValid(Controller)
            || Character->GetController() != Controller || Controller->GetPawn() != Character)
        {
            return;
        }
        Controller->SetControlRotation(InArrival);
    }
} // namespace

bool UCharacterTeleportLibrary::TeleportCharacter(const UObject* InWorldContext,
    ACharacter* InCharacter,
    FVector InGroundLocation,
    FRotator InFacing,
    FText& OutFailure)
{
    OutFailure = FText::GetEmpty();
    if (!IsInGameThread())
    {
        OutFailure = FText::FromString(TEXT("Teleport must run on the game thread."));
        return false;
    }
    UWorld* World =
        GEngine && IsValid(InWorldContext)
            ? GEngine->GetWorldFromContextObject(InWorldContext, EGetWorldErrorMode::ReturnNull)
            : nullptr;
    if (!IsLiveCharacter(InCharacter, World) || InGroundLocation.ContainsNaN()
        || InFacing.ContainsNaN())
    {
        OutFailure =
            FText::FromString(TEXT("Teleport requires a live character in the context world."));
        return false;
    }
    FVector Center;
    if (!FindDestination(*InCharacter, InGroundLocation, Center, OutFailure))
    {
        return false;
    }
    const FRotator Arrival(0, InFacing.Yaw, 0);
    const FPlacementObservation Observation(*InCharacter);
    if (!InCharacter->TeleportTo(Center, Arrival))
    {
        OutFailure = FText::FromString(TEXT("Teleport could not place the character safely."));
        return false;
    }
    FinishPlacement(Observation, Arrival);
    return true;
}
