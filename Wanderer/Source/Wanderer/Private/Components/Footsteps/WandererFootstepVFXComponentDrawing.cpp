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

void UWandererFootstepVFXComponent::EmitFootstep(ACharacter& Character)
{
    const ICharacterSurfaceProvider* Surface =
        Cast<ICharacterSurfaceProvider>(Character.GetController());
    if (!Surface
        || Surface->QueryContactSurface(nullptr, Character.GetActorLocation()).Wetness
               == TEXT("wet"))
    {
        return;
    }
    const FName FootName = bLeftFoot ? LeftFootBone : RightFootBone;
    FVector Foot = Character.GetActorLocation()
                   - FVector(0, 0, Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    Foot +=
        Character.GetActorRightVector()
        * (bLeftFoot ? -FootEffectSettings::FootSideOffset : FootEffectSettings::FootSideOffset);
    if (Character.GetMesh()->DoesSocketExist(FootName))
    {
        Foot = Character.GetMesh()->GetSocketLocation(FootName);
    }
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererFootstep), true, &Character);
    Query.bReturnPhysicalMaterial = true;
    Query.bReturnFaceIndex = true;
    if (!GetWorld()->LineTraceSingleByChannel(Hit,
            Foot + FVector(0, 0, FootEffectSettings::TraceHeight),
            Foot - FVector(0, 0, FootEffectSettings::TraceDepth),
            ECC_Visibility,
            Query))
    {
        return;
    }
    const FName Ground = Surface->ResolveSurfaceName(Hit);
    const int32 SurfaceIndex = Ground == TEXT("soil")   ? 0
                               : Ground == TEXT("sand") ? 1
                               : Ground == TEXT("snow") ? 2
                                                        : INDEX_NONE;
    if (SurfaceIndex == INDEX_NONE || Hit.ImpactNormal.Z < FootEffectSettings::MinimumNormalZ)
    {
        return;
    }
    EmitDust(Hit, SurfaceIndex);
    if (bStopped || !IsValid(&Character) || !Character.IsLocallyControlled())
    {
        return;
    }
    if (!EmitPrint(Character, Hit, SurfaceIndex))
    {
        return;
    }
    ++FootstepCount;
}

void UWandererFootstepVFXComponent::EmitDust(const FHitResult& Hit, int32 SurfaceIndex)
{
    const float DustScale = SurfaceIndex == 1 ? FootEffectSettings::SandDustScale
                                              : FootEffectSettings::DefaultDustScale;
    if (DustTemplates.IsValidIndex(SurfaceIndex) && DustTemplates[SurfaceIndex]
        && PrepareDust(DustTemplates[SurfaceIndex]))
    {
        UParticleSystemComponent* Dust = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(),
            DustTemplates[SurfaceIndex],
            Hit.ImpactPoint + Hit.ImpactNormal,
            FRotator::ZeroRotator,
            FVector(DustScale));
        if (Dust && FootEffectSettings::HasUnmaskedSecondaryEmitter(*DustTemplates[SurfaceIndex]))
        {
            // This imported secondary emitter uses M_SoftGradient_Muzzle without an
            // opacity output. Keep the masked Smoke puff, not the unrelated sprite quad.
            Dust->SetEmitterEnable(TEXT("Particle Emitter"), false);
        }
        AddEffect(Dust, FootEffectSettings::DustLifetime);
    }
    else
    {
        ++DeferredEffectCount;
    }
}

bool UWandererFootstepVFXComponent::EmitPrint(
    ACharacter& Character, const FHitResult& Hit, int32 SurfaceIndex)
{
    const ICharacterSurfaceProvider* Surface =
        Cast<ICharacterSurfaceProvider>(Character.GetController());
    if (!Surface)
    {
        return false;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererFootstep), true, &Character);
    Query.bReturnPhysicalMaterial = true;
    Query.bReturnFaceIndex = true;
    const FName Ground = Surface->ResolveSurfaceName(Hit);
    const int32 PrintIndex = SurfaceIndex * 2 + (bLeftFoot ? 0 : 1);
    if (PrintMaterials.IsValidIndex(PrintIndex) && PrintMaterials[PrintIndex])
    {
        // A missing opacity mask would expose the full rectangular decal volume.
        // Skip this step while resources stream, rather than show a fallback quad.
        if (!PrepareMaterial(PrintMaterials[PrintIndex]))
        {
            ++DeferredEffectCount;
            return false;
        }
        const FVector Forward =
            FVector::VectorPlaneProject(Character.GetActorForwardVector(), Hit.ImpactNormal)
                .GetSafeNormal();
        const FVector Right = FVector::CrossProduct(Hit.ImpactNormal, Forward);
        // A decal spans both sides of its centre. Do not project a snow/sand print
        // across a visual material edge even when its centre is still inside.
        for (const FVector2D Corner :
            {FVector2D(-17, -10), FVector2D(-17, 10), FVector2D(17, -10), FVector2D(17, 10)})
        {
            const FVector Point = Hit.ImpactPoint + Forward * Corner.X + Right * Corner.Y;
            FHitResult Edge;
            if (!GetWorld()->LineTraceSingleByChannel(Edge,
                    Point + FVector(0, 0, 20),
                    Point - FVector(0, 0, 35),
                    ECC_Visibility,
                    Query)
                || Surface->ResolveSurfaceName(Edge) != Ground)
            {
                return false;
            }
        }
        const FRotator Rotation =
            FRotationMatrix::MakeFromXY(-Hit.ImpactNormal, -Forward).Rotator();
        UDecalComponent* Print = UGameplayStatics::SpawnDecalAtLocation(GetWorld(),
            PrintMaterials[PrintIndex],
            FVector(FootEffectSettings::PrintDepth,
                FootEffectSettings::PrintHalfLength,
                FootEffectSettings::PrintHalfWidth),
            Hit.ImpactPoint + Hit.ImpactNormal,
            Rotation,
            FootEffectSettings::PrintLifetime);
        if (Print)
        {
            Print->SetFadeOut(
                FootEffectSettings::FadeStartSeconds, FootEffectSettings::FadeSeconds, false);
            AddEffect(Print, FootEffectSettings::PrintLifetime);
        }
    }
    return true;
}
