#include "Components/SurfaceInteraction/WandererNaturalInteractionComponent.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

#include "Components/SurfaceInteraction/WandererInteractionBridge.h"
#include "Data/DemoSurfaceData.h"
#include "Data/SurfaceInteractionData.h"

UWandererNaturalInteractionComponent::UWandererNaturalInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = UE::Wanderer::SurfaceInteraction::GrassTickInterval;
}

void UWandererNaturalInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    if (!World || !World->GetMapName().EndsWith(UE::Wanderer::DemoSurface::MapSuffix))
    {
        SetComponentTickEnabled(false);
        return;
    }
    bStopped = false;
    UMaterialParameterCollection* Loaded = GrassParameterAsset.LoadSynchronous();
    if (!CanInteract())
    {
        return;
    }
    GrassParameters = Loaded;
    if (!GrassParameters)
    {
        UE_LOG(LogTemp,
            Warning,
            TEXT("Grass interaction asset unavailable; grass response is disabled."));
    }
    InitializeSnow();
}

void UWandererNaturalInteractionComponent::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    bStopped = true;
    SetComponentTickEnabled(false);
    GrassParameters = nullptr;
    UActorComponent* Owned = SnowDisplacementComponent;
    SnowDisplacementComponent = nullptr;
    if (IsValid(Owned))
    {
        Owned->DestroyComponent();
    }
    Super::EndPlay(InEndPlayReason);
}

void UWandererNaturalInteractionComponent::TickComponent(
    float InDeltaTime, ELevelTick InTickType, FActorComponentTickFunction* InTickFunction)
{
    Super::TickComponent(InDeltaTime, InTickType, InTickFunction);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!CanInteract() || !Character || !Character->IsLocallyControlled() || !GrassParameters)
    {
        return;
    }
    const FVector Feet =
        Character->GetActorLocation()
        - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    UKismetMaterialLibrary::SetVectorParameterValue(GetWorld(),
        GrassParameters,
        TEXT("PlayerPosition"),
        FLinearColor(
            Feet.X, Feet.Y, Feet.Z, UE::Wanderer::SurfaceInteraction::GrassResponseRadius));
}

void UWandererNaturalInteractionComponent::InitializeSnow()
{
    for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
    {
        if (!Actor->ActorHasTag(TEXT("TrialSnowInteraction")))
        {
            continue;
        }
        UActorComponent* Created =
            UWandererInteractionBridge::CreateSnowDisplacement(Cast<ACharacter>(GetOwner()));
        // RegisterComponent can execute Blueprint BeginPlay and end the owning character.
        if (!CanInteract())
        {
            if (IsValid(Created))
            {
                Created->DestroyComponent();
            }
            return;
        }
        SnowDisplacementComponent = Created;
        if (Created)
        {
            return;
        }
    }
}

bool UWandererNaturalInteractionComponent::CanInteract() const
{
    return !bStopped && IsValid(this) && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
           && IsValid(GetWorld());
}
