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

void UWandererFootstepVFXComponent::InitializeAssetReferences()
{
    for (const TCHAR* Name : {TEXT("GenericDust"), TEXT("Sand"), TEXT("Snow")})
    {
        const FString Path = FString(FootEffectSettings::DustRoot) + TEXT("P_Footstep_") + Name;
        DustAssets.Add(TSoftObjectPtr<UParticleSystem>(
            FSoftObjectPath(Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path))));
    }
    for (const TCHAR* Surface : {TEXT("Soil"), TEXT("Sand"), TEXT("Snow")})
    {
        for (const TCHAR* Foot : {TEXT("Left"), TEXT("Right")})
        {
            const FString Name =
                FString(Surface) == TEXT("Soil")
                    ? TEXT("MI_TrialSoil_")
                    : FString(TEXT("MI_Decal_footstep_print_Inst_")) + Surface + TEXT("_");
            const FString Path =
                (FString(Surface) == TEXT("Soil") ? FString(TEXT("/Game/Materials/Ground/Soil/"))
                                                  : FString(FootEffectSettings::PrintRoot))
                + Name + Foot;
            PrintAssets.Add(TSoftObjectPtr<UMaterialInterface>(
                FSoftObjectPath(Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path))));
        }
    }
}

void UWandererFootstepVFXComponent::LoadEffects()
{
    for (const TSoftObjectPtr<UParticleSystem>& Asset : DustAssets)
    {
        DustTemplates.Add(Asset.LoadSynchronous());
    }
    for (const TSoftObjectPtr<UMaterialInterface>& Asset : PrintAssets)
    {
        TSoftObjectPtr<UMaterialInterface> SelectedAsset = Asset;
        for (const TCHAR* Foot : {TEXT("Left"), TEXT("Right")})
        {
            const FString OriginalSnow = FString(FootEffectSettings::PrintRoot)
                                         + TEXT("MI_Decal_footstep_print_Inst_Snow_") + Foot;
            if (Asset.ToSoftObjectPath().GetLongPackageName() == OriginalSnow)
            {
                const FString RevisedSnow =
                    FString(TEXT("/Game/Materials/Ground/Footprints/MI_SnowImpression_")) + Foot;
                if (FPackageName::DoesPackageExist(RevisedSnow))
                {
                    SelectedAsset = TSoftObjectPtr<UMaterialInterface>(
                        FSoftObjectPath(RevisedSnow + TEXT(".")
                                        + FPackageName::GetLongPackageAssetName(RevisedSnow)));
                }
            }
            const FString Original = FString(FootEffectSettings::PrintRoot)
                                     + TEXT("MI_Decal_footstep_print_Inst_Sand_") + Foot;
            if (Asset.ToSoftObjectPath().GetLongPackageName() != Original)
            {
                continue;
            }
            const FString Revised =
                FString(TEXT("/Game/Materials/Ground/Footprints/MI_SandImpression_")) + Foot;
            if (!FPackageName::DoesPackageExist(Revised))
            {
                continue;
            }
            SelectedAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
                Revised + TEXT(".") + FPackageName::GetLongPackageAssetName(Revised)));
        }
        UMaterialInterface* Material = SelectedAsset.LoadSynchronous();
        if (!Material && SelectedAsset.ToSoftObjectPath() != Asset.ToSoftObjectPath())
        {
            UE_LOG(LogTemp,
                Warning,
                TEXT("Trial revised impression unavailable; using original footstep material."));
            Material = Asset.LoadSynchronous();
        }
        PrintMaterials.Add(Material);
    }
    if (DustTemplates.Contains(nullptr) || PrintMaterials.Contains(nullptr))
    {
        UE_LOG(LogTemp,
            Warning,
            TEXT("Trial footstep effects incomplete; unavailable dust/prints will be skipped."));
    }
    // Request the small opacity/normal textures before their first visible use.
    // Package loading alone does not make streamed texture mips render-ready.
    for (UMaterialInterface* Material : PrintMaterials)
    {
        PrepareMaterial(Material);
    }
    for (UParticleSystem* Template : DustTemplates)
    {
        PrepareDust(Template);
    }
}

bool UWandererFootstepVFXComponent::PrepareMaterial(UMaterialInterface* Material) const
{
    if (!Material || !GetWorld())
    {
        return false;
    }
    const ERHIFeatureLevel::Type FeatureLevel = GetWorld()->GetFeatureLevel();
    FMaterialResource* Resource = Material->GetMaterialResource(FeatureLevel);
#if WITH_EDITOR
    // UE's on-demand compiler may report IsCompiling=false before the first
    // draw submits its jobs. Submit now; waiting without submitting deadlocks
    // readiness, while drawing first exposes the temporary fallback rectangle.
    if (Resource && !Resource->IsGameThreadShaderMapComplete())
    {
        Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
    }
#endif
    bool bReady = !Material->IsCompiling() && Resource && Resource->IsGameThreadShaderMapComplete();
    TArray<UTexture*> Textures;
    Material->GetUsedTextures(Textures, EMaterialQualityLevel::Num, false, FeatureLevel, false);
    for (UTexture* Texture : Textures)
    {
        if (!Texture)
        {
            bReady = false;
            continue;
        }
        // A bounded first-use warmup; do not shorten another user's residency
        // request (including an active cinematic request) on shared textures.
        if (!Texture->ShouldMipLevelsBeForcedResident())
        {
            Texture->SetForceMipLevelsToBeResident(FootEffectSettings::PrintLifetime + 1.0f);
        }
#if WITH_EDITOR
        if (Texture->IsCompiling())
        {
            bReady = false;
        }
#endif
        if (!Texture->GetResource() || Texture->HasPendingInitOrStreaming()
            || !Texture->IsFullyStreamedIn())
        {
            bReady = false;
        }
    }
    return bReady;
}

bool UWandererFootstepVFXComponent::PrepareDust(UParticleSystem* Template) const
{
    if (!Template)
    {
        return false;
    }
    const bool bSuppressSecondary = FootEffectSettings::HasUnmaskedSecondaryEmitter(*Template);
    bool bReady = true;
    for (const UParticleEmitter* Emitter : Template->Emitters)
    {
        if (!Emitter || (bSuppressSecondary && Emitter->EmitterName == TEXT("Particle Emitter")))
        {
            continue;
        }
        for (const UParticleLODLevel* Level : Emitter->LODLevels)
        {
            if (Level && Level->bEnabled && Level->RequiredModule
                && !PrepareMaterial(Level->RequiredModule->Material))
            {
                bReady = false;
            }
        }
    }
    return bReady;
}
