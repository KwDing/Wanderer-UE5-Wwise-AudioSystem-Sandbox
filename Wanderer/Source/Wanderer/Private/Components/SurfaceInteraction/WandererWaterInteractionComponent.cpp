#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"

#include "NiagaraSystem.h"

#include "Components/SurfaceInteraction/WandererInteractionBridge.h"
#include "Data/DemoSurfaceData.h"
#include "Data/SurfaceInteractionData.h"

UWandererWaterInteractionComponent::UWandererWaterInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.0f;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    for (int32 Index = 0; Index < UE::Wanderer::Water::MaxBodySections; ++Index)
    {
        BodyStartParameters.Add(FName(*FString::Printf(TEXT("BodyStart%d"), Index)));
        BodyEndParameters.Add(FName(*FString::Printf(TEXT("BodyEnd%d"), Index)));
        BodyRadiusParameters.Add(FName(*FString::Printf(TEXT("BodyRadius%d"), Index)));
    }
    for (int32 Index = 0; Index < UE::Wanderer::SurfaceInteraction::MaximumContactWaves; ++Index)
    {
        WavePointParameters.Add(FName(*FString::Printf(TEXT("WavePoint%d"), Index)));
        WaveDataParameters.Add(FName(*FString::Printf(TEXT("WaveData%d"), Index)));
    }
}

#if WITH_EDITOR
void UWandererWaterInteractionComponent::AppendWaterPackagesToCook(
    TArray<FName>& InOutPackages) const
{
    InOutPackages.AddUnique(WaterlineMeshAsset.ToSoftObjectPath().GetLongPackageFName());
    InOutPackages.AddUnique(WaterlineTemplateAsset.ToSoftObjectPath().GetLongPackageFName());
    InOutPackages.AddUnique(SprayTemplateAsset.ToSoftObjectPath().GetLongPackageFName());
    InOutPackages.AddUnique(SwimmingSprayTemplateAsset.ToSoftObjectPath().GetLongPackageFName());
}
#endif

void UWandererWaterInteractionComponent::ClearWaterRegistration()
{
    if (!IsInGameThread())
    {
        return;
    }
    bPendingClear = true;
    if (bUpdating || bClearing)
    {
        return;
    }
    bClearing = true;
    bPendingClear = false;
    // Detach before Blueprint removal events; callbacks may request cleanup again.
    const TSet<TWeakObjectPtr<AActor>> Registrations = MoveTemp(RegisteredWaterActors);
    for (FWandererIWSReceiver& Receiver : IWSReceivers)
    {
        ReleaseSurfaceMaterial(Receiver);
        ReleaseBodyWaterline(Receiver);
        ResetReceiverContact(Receiver);
    }
    PruneSplashes(true);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (IsValid(Character))
    {
        for (const TWeakObjectPtr<AActor>& Observed : Registrations)
        {
            UWandererInteractionBridge::UnregisterWaterCharacterMesh(Observed.Get(), Character);
        }
    }
    bClearing = false;
    bPendingClear = false;
    if (bStopped)
    {
        ReleaseWaterAssets();
    }
}

int32 UWandererWaterInteractionComponent::GetActiveWaterlineCount() const
{
    int32 Count = 0;
    for (const FWandererIWSReceiver& Receiver : IWSReceivers)
    {
        if (Receiver.WaterlineWeakPtr.IsValid())
        {
            ++Count;
        }
    }
    return Count;
}

int32 UWandererWaterInteractionComponent::GetWaterlineSectionCount() const
{
    int32 Count = 0;
    for (const FWandererIWSReceiver& Receiver : IWSReceivers)
    {
        Count += Receiver.BodySections.Num();
    }
    return Count;
}

void UWandererWaterInteractionComponent::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    if (!World || !World->GetMapName().EndsWith(UE::Wanderer::DemoSurface::MapSuffix))
    {
        SetComponentTickEnabled(false);
        return;
    }
    bStopped = false;
    bUpdating = true;
    ON_SCOPE_EXIT
    {
        FinishUpdate();
    };
    if (!LoadWaterAssets() || !CanUpdate())
    {
        return;
    }
    for (TActorIterator<AActor> Actor(World); Actor; ++Actor)
    {
        if (Actor->ActorHasTag(TEXT("TrialWaterInteraction")))
        {
            WaterInteractionActors.Add(*Actor);
        }
    }
    FindIWSReceivers();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        AddTickPrerequisiteComponent(Character->GetMesh());
    }
}

void UWandererWaterInteractionComponent::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    bStopped = true;
    SetComponentTickEnabled(false);
    ClearWaterRegistration();
    Super::EndPlay(InEndPlayReason);
}

void UWandererWaterInteractionComponent::TickComponent(
    float InDeltaTime, ELevelTick InTickType, FActorComponentTickFunction* InTickFunction)
{
    Super::TickComponent(InDeltaTime, InTickType, InTickFunction);
    if (!CanUpdate() || bUpdating || bClearing)
    {
        return;
    }
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Character->IsLocallyControlled())
    {
        ClearWaterRegistration();
        return;
    }
    bUpdating = true;
    ON_SCOPE_EXIT
    {
        FinishUpdate();
    };
    PruneSplashes(false);
    for (FWandererIWSReceiver& Receiver : IWSReceivers)
    {
        if (!CanUpdate())
        {
            return;
        }
        UpdateIWSReceiver(Receiver, *Character);
    }
    UpdateLegacyRegistrations(*Character);
}

bool UWandererWaterInteractionComponent::CanUpdate() const
{
    return !bStopped && !bPendingClear && IsValid(this) && IsValid(GetWorld())
           && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed();
}

void UWandererWaterInteractionComponent::FinishUpdate()
{
    bUpdating = false;
    if (bPendingClear || bStopped)
    {
        ClearWaterRegistration();
    }
}

bool UWandererWaterInteractionComponent::LoadWaterAssets()
{
    UStaticMesh* LoadedMesh = WaterlineMeshAsset.LoadSynchronous();
    if (!CanUpdate())
    {
        return false;
    }
    WaterlineMesh = LoadedMesh;
    UMaterialInterface* LoadedMaterial = WaterlineTemplateAsset.LoadSynchronous();
    if (!CanUpdate())
    {
        return false;
    }
    WaterlineTemplate = LoadedMaterial;
    UNiagaraSystem* LoadedSpray = SprayTemplateAsset.LoadSynchronous();
    if (!CanUpdate())
    {
        return false;
    }
    SprayTemplate = LoadedSpray;
    UNiagaraSystem* LoadedSwimmingSpray = SwimmingSprayTemplateAsset.LoadSynchronous();
    if (!CanUpdate())
    {
        return false;
    }
    SwimmingSprayTemplate = LoadedSwimmingSpray;
    ensureMsgf(SwimmingSprayTemplate, TEXT("Swimming contact spray asset unavailable."));
    ensureMsgf(WaterlineMesh && WaterlineTemplate, TEXT("Body waterline assets unavailable."));
    ensureMsgf(SprayTemplate, TEXT("Contact spray asset unavailable."));
    return true;
}

void UWandererWaterInteractionComponent::ReleaseWaterAssets()
{
    for (const FWandererIWSReceiver& Receiver : IWSReceivers)
    {
        if (AActor* Solver = Receiver.SolverWeakPtr.Get())
        {
            RemoveTickPrerequisiteActor(Solver);
        }
    }
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        RemoveTickPrerequisiteComponent(Character->GetMesh());
    }
    WaterInteractionActors.Reset();
    IWSReceivers.Reset();
    WaterlineMesh = nullptr;
    WaterlineTemplate = nullptr;
    SprayTemplate = nullptr;
    SwimmingSprayTemplate = nullptr;
}

void UWandererWaterInteractionComponent::ResetReceiverContact(FWandererIWSReceiver& InOutReceiver)
{
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
