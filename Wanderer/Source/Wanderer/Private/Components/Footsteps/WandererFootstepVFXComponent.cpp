#include "Components/Footsteps/WandererFootstepVFXComponent.h"

#include "AnimationRuntime.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

#include "Components/Footsteps/FootEffectUtils.h"
#include "Data/FootEffectData.h"
#include "Interfaces/CharacterSurfaceProvider.h"
#include "Interfaces/FootContactHost.h"

UWandererFootstepVFXComponent::UWandererFootstepVFXComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = FootEffectSettings::TickSeconds;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    InitializeAssetReferences();
}
FFootContactSettings UWandererFootstepVFXComponent::GetContactSettings() const
{
    FFootContactSettings Result;
    Result.LeftFootBone = LeftFootBone;
    Result.RightFootBone = RightFootBone;
    Result.ContactHeight = ContactHeight;
    Result.ReleaseHeight = ReleaseHeight;
    Result.GroundSupportTolerance = GroundSupportTolerance;
    Result.ContactConfirmationSeconds = ContactConfirmationSeconds;
    Result.GroundTraceHeight = GroundTraceHeight;
    Result.GroundTraceDepth = GroundTraceDepth;
    Result.WalkingGateSeconds = WalkingGateSeconds;
    Result.RunningGateSeconds = RunningGateSeconds;
    Result.RunningSpeedThreshold = RunningSpeedThreshold;
    Result.SwimStrokeReachRatio = SwimStrokeReachRatio;
    Result.TreadingReachRatio = TreadingReachRatio;
    Result.MinimumSwimHandSpeed = MinimumSwimHandSpeed;
    return Result;
}
void UWandererFootstepVFXComponent::EmitContact(bool bInLeftFoot)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (bStopped || !bVisualEffectsEnabled || !IsValid(Character)
        || !Character->IsLocallyControlled() || !GetWorld())
    {
        return;
    }
    bLeftFoot = bInLeftFoot;
    EmitFootstep(*Character);
}
void UWandererFootstepVFXComponent::Stop()
{
    bStopped = true;
    SetComponentTickEnabled(false);
    ClearEffects();
    DustTemplates.Reset();
    PrintMaterials.Reset();
}
int32 UWandererFootstepVFXComponent::GetFootContactCount() const
{
    const IFootContactHost* Host = Cast<IFootContactHost>(GetOwner());
    return Host ? Host->GetContactCount() : 0;
}
FWandererFootContactDebug UWandererFootstepVFXComponent::GetFootContactDebug(FName FootBone) const
{
    const IFootContactHost* Host = Cast<IFootContactHost>(GetOwner());
    return Host ? Host->QueryFootContact(FootBone) : FWandererFootContactDebug();
}
int32 UWandererFootstepVFXComponent::GetSwimStrokeCount() const
{
    const IFootContactHost* Host = Cast<IFootContactHost>(GetOwner());
    return Host ? Host->GetStrokeCount() : 0;
}
void UWandererFootstepVFXComponent::BeginPlay()
{
    Super::BeginPlay();
    bVisualEffectsEnabled =
        GetWorld() && GetWorld()->GetMapName().EndsWith(TEXT("L_AudioSystemDemo"));
    if (bVisualEffectsEnabled)
    {
        Effects.Reserve(FootEffectSettings::MaximumEffects);
        LoadEffects();
    }
}
void UWandererFootstepVFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    Super::EndPlay(EndPlayReason);
}
void UWandererFootstepVFXComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (bStopped || !GetWorld())
    {
        return;
    }
    PruneEffects();
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (Character && !Character->IsLocallyControlled())
    {
        ClearEffects();
    }
}

void UWandererFootstepVFXComponent::AddEffect(USceneComponent* Component, float Lifetime)
{
    if (!Component)
    {
        return;
    }
    if (bStopped || bClearingEffects || !GetWorld())
    {
        Component->DestroyComponent();
        return;
    }
    if (Effects.Num() >= FootEffectSettings::MaximumEffects)
    {
        TWeakObjectPtr<USceneComponent> OldestWeakPtr = Effects[0].Component;
        Effects.RemoveAt(0);
        if (OldestWeakPtr.IsValid())
        {
            OldestWeakPtr->DestroyComponent();
        }
        if (bStopped)
        {
            Component->DestroyComponent();
            return;
        }
    }
    FWandererTimedFootEffect Effect;
    Effect.Component = Component;
    Effect.ExpiresAt = GetWorld()->GetTimeSeconds() + Lifetime;
    Effects.Add(Effect);
}

void UWandererFootstepVFXComponent::PruneEffects()
{
    const double Now = GetWorld()->GetTimeSeconds();
    TArray<TWeakObjectPtr<USceneComponent>, TInlineAllocator<FootEffectSettings::MaximumEffects>>
        Expired;
    for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
    {
        if (Effects[Index].Component.IsValid() && Now < Effects[Index].ExpiresAt)
        {
            continue;
        }
        Expired.Add(Effects[Index].Component);
        Effects.RemoveAt(Index);
    }
    for (const TWeakObjectPtr<USceneComponent>& EffectWeakPtr : Expired)
    {
        if (EffectWeakPtr.IsValid())
        {
            EffectWeakPtr->DestroyComponent();
        }
    }
}

void UWandererFootstepVFXComponent::ClearEffects()
{
    if (bClearingEffects)
    {
        return;
    }
    TGuardValue<bool> ClearingGuard(bClearingEffects, true);
    TArray<FWandererTimedFootEffect> Removed = MoveTemp(Effects);
    Effects.Reset();
    for (const FWandererTimedFootEffect& Effect : Removed)
    {
        if (USceneComponent* Component = Effect.Component.Get())
        {
            Component->DestroyComponent();
        }
    }
}
