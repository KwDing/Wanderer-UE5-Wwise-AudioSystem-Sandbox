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

void UWandererSurfaceComponent::SampleSurface()
{
    if (bStopped || bPublishing)
    {
        return;
    }
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character || !Controller->IsLocalController())
    {
        return;
    }
    if (ObservedCharacter.Get() != Character)
    {
        ObservedCharacter = Character;
        CandidateSurface = NAME_None;
        CandidateSamples = 0;
    }
    if (!Character->GetCharacterMovement()->IsMovingOnGround())
    {
        if (Character->GetCharacterMovement()->IsSwimming())
        {
            PublishContact(TEXT("water"), TEXT("wet"));
            return;
        }
        CandidateSurface = NAME_None;
        CandidateSamples = 0;
        return;
    }
    const FVector Feet =
        Character->GetActorLocation()
        - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WandererSurface), true, Character);
    Params.bReturnPhysicalMaterial = true;
    Params.bReturnFaceIndex = true;
    // Visual puddle planes are masks, not the supporting floor. Trace the actual
    // floor/cover beneath them, then determine wetness independently.
    for (const TWeakObjectPtr<UStaticMeshComponent>& Puddle : Puddles)
    {
        if (Puddle.IsValid())
        {
            Params.AddIgnoredComponent(Puddle.Get());
        }
    }
    for (const TWeakObjectPtr<UStaticMeshComponent>& Water : PoolWaterSurfaces)
    {
        if (Water.IsValid())
        {
            Params.AddIgnoredComponent(Water.Get());
        }
    }
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(
            Hit, Feet + FVector(0, 0, 20), Feet - FVector(0, 0, 35), ECC_Visibility, Params))
    {
        // Capsule support can be off-center on a ledge/statue. Do not retain an old
        // contact state merely because the narrow center ray missed that support.
        const FHitResult& Support = Character->GetCharacterMovement()->CurrentFloor.HitResult;
        if (!Support.bBlockingHit || !Support.GetComponent())
        {
            CandidateSamples = 0;
            return;
        }
        Hit = Support;
    }
    PublishContact(ResolveSurface(Hit), ResolveContactWetness(Hit, Feet));
}

void UWandererSurfaceComponent::PublishContact(FName Surface, FName Wetness)
{
    if (bStopped || bPublishing)
    {
        return;
    }
    TGuardValue<bool> PublishingGuard(bPublishing, true);
    const TWeakObjectPtr<UWandererSurfaceComponent> SelfWeakPtr(this);
    const TWeakObjectPtr<UWorld> WorldWeakPtr(GetWorld());
    const TWeakObjectPtr<AActor> OwnerWeakPtr(GetOwner());
    const APlayerController* Player = Cast<APlayerController>(GetOwner());
    const TWeakObjectPtr<APawn> PawnWeakPtr(Player ? Player->GetPawn() : nullptr);
    const auto CanPublish = [SelfWeakPtr, WorldWeakPtr, OwnerWeakPtr, PawnWeakPtr]()
    {
        const UWandererSurfaceComponent* Self = SelfWeakPtr.Get();
        if (!Self || Self->bStopped || !WorldWeakPtr.IsValid() || !OwnerWeakPtr.IsValid()
            || Self->GetWorld() != WorldWeakPtr.Get())
        {
            return false;
        }
        const APlayerController* CurrentPlayer = Cast<APlayerController>(OwnerWeakPtr.Get());
        return !CurrentPlayer || CurrentPlayer->GetPawn() == PawnWeakPtr.Get();
    };

    if (Surface == CurrentSurface && Wetness == CurrentWetness)
    {
        CandidateSamples = 0;
        return;
    }
    if (CandidateSurface != Surface || CandidateWetness != Wetness)
    {
        CandidateSurface = Surface;
        CandidateWetness = Wetness;
        CandidateSamples = 0;
    }
    ++CandidateSamples;
    const bool bInitial = CurrentSurface.IsNone();
    if (!bInitial && CandidateSamples < 2)
    {
        return;
    }
    const FName PreviousSurface = CurrentSurface;
    const FName PreviousWetness = CurrentWetness;
    // Commit both fields before notifying either listener, so snapshots are coherent.
    CurrentSurface = Surface;
    CurrentWetness = Wetness;
    CandidateSamples = 0;
    if (bInitial || PreviousSurface != CurrentSurface)
    {
        OnSurfaceChanged.Broadcast(PreviousSurface, CurrentSurface);
        if (!CanPublish())
        {
            return;
        }
        PublishContactEvent(EWandererGameEvent::GroundSurface, PreviousSurface, PreviousWetness);
    }
    if (!CanPublish())
    {
        return;
    }
    if (bInitial || PreviousWetness != CurrentWetness)
    {
        OnWetnessChanged.Broadcast(PreviousWetness, CurrentWetness);
        if (!CanPublish())
        {
            return;
        }
        PublishContactEvent(EWandererGameEvent::GroundWetness, PreviousSurface, PreviousWetness);
    }
}

void UWandererSurfaceComponent::PublishContactEvent(
    EWandererGameEvent Type, FName PreviousSurface, FName PreviousWetness)
{
    UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    UWandererEventBusSubsystem* Bus =
        Instance ? Instance->GetSubsystem<UWandererEventBusSubsystem>() : nullptr;
    if (Bus)
    {
        FWandererGameEventMessage Message;
        Message.Type = Type;
        Message.Source = TEXT("SurfaceListener");
        Message.PreviousSurface = PreviousSurface;
        Message.Surface = CurrentSurface;
        Message.PreviousWetness = PreviousWetness;
        Message.Wetness = CurrentWetness;
        Message.Text = FString::Printf(TEXT("Surface: %s -> %s | Wetness: %s -> %s"),
            *PreviousSurface.ToString(),
            *CurrentSurface.ToString(),
            *PreviousWetness.ToString(),
            *CurrentWetness.ToString());
        Bus->Publish(Message);
    }
}
