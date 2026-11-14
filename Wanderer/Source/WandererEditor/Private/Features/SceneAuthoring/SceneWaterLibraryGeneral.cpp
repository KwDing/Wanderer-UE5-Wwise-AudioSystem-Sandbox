#include "Features/SceneAuthoring/SceneWaterLibrary.h"

#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Engine/World.h"
#include "GameFramework/PhysicsVolume.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectHash.h"

#include "Editing/VolumeBrushUtils.h"
#include "Features/SceneAuthoring/SceneAuthoringData.h"
#include "Features/SceneAuthoring/SceneAuthoringUtils.h"
#include "Foundation/AuthoringLog.h"

namespace
{
    using namespace SceneAuthoringParameters;

    void ResizeSwimmingRenderers(UNiagaraSystem* InSystem)
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(InSystem, Objects, true);
        for (UObject* Object : Objects)
        {
            UNiagaraMeshRendererProperties* Renderer = Cast<UNiagaraMeshRendererProperties>(Object);
            if (!Renderer || Renderer->GetOutermost() != InSystem->GetOutermost())
            {
                continue;
            }
            Renderer->Modify();
            for (FNiagaraMeshRendererMeshProperties& Mesh : Renderer->Meshes)
            {
                Mesh.Scale = FVector(SwimmingSprayScale);
            }
            Renderer->PostEditChange();
        }
    }

    bool CompileWaterSystem(UNiagaraSystem* InSystem)
    {
        InSystem->PostEditChange();
        InSystem->RequestCompile(false);
        InSystem->WaitForCompilationComplete(true, false);
        InSystem->MarkPackageDirty();
        return InSystem->IsReadyToRun();
    }
} // namespace

bool USceneWaterLibrary::ConfigureSwimmingContactSpray(UNiagaraSystem* System)
{
    if (!IsInGameThread() || !IsValid(System)
        || System->GetPathName()
               != TEXT("/Game/VFX/Niagara/Systems/Water/NS_SwimmingContact.NS_SwimmingContact"))
    {
        return false;
    }
    if (System->GetEmitterHandles().IsEmpty())
    {
        return false;
    }
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (!Handle.GetEmitterData() || !Handle.GetInstance().Emitter
            || Handle.GetInstance().Emitter->GetOutermost() != System->GetOutermost())
        {
            return false;
        }
    }
    System->Modify();
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        Handle.GetInstance().Emitter->Modify();
        Handle.GetEmitterData()->bLocalSpace = true;
        Handle.GetInstance().Emitter->PostEditChange();
    }
    ResizeSwimmingRenderers(System);
    return CompileWaterSystem(System);
}

bool USceneWaterLibrary::ConfigureTrialDropletRenderer(
    UNiagaraSystem* System, UMaterialInterface* DropletMaterial, float MeshScale)
{
    if (!IsInGameThread() || !SceneAuthoringUtils::IsAllowedAsset(System, DropletSystems)
        || !SceneAuthoringUtils::IsAllowedAsset(DropletMaterial, DropletMaterials)
        || !FMath::IsFinite(MeshScale) || MeshScale < MinDropletScale
        || MeshScale > MaxDropletScale)
    {
        return false;
    }
    TArray<UObject*> Objects;
    GetObjectsWithOuter(System, Objects, true);
    bool bConfigured = false;
    for (UObject* Object : Objects)
    {
        UNiagaraMeshRendererProperties* Renderer = Cast<UNiagaraMeshRendererProperties>(Object);
        if (!Renderer || Renderer->GetOutermost() != System->GetOutermost())
        {
            continue;
        }
        Renderer->Modify();
        for (FNiagaraMeshRendererMeshProperties& Mesh : Renderer->Meshes)
        {
            Mesh.Scale = FVector(MeshScale);
        }
        Renderer->bOverrideMaterials = true;
        Renderer->OverrideMaterials.SetNum(1);
        Renderer->OverrideMaterials[0].ExplicitMat = DropletMaterial;
        Renderer->PostEditChange();
        bConfigured = true;
    }
    if (!bConfigured)
    {
        return false;
    }
    System->Modify();
    return CompileWaterSystem(System);
}

APhysicsVolume* USceneWaterLibrary::CreateWaterVolume(UObject* WorldContext,
    const TArray<FVector2D>& Outline,
    const FVector& SurfaceLocation,
    float Depth)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor || SurfaceLocation.ContainsNaN()
        || !WandererVolumeBrush::IsValidVolumeOutline(Outline, Depth))
    {
        UE_LOG(LogWandererAuthoring,
            Warning,
            TEXT("Water volume requires an editor world, finite position/"
                 "depth and a convex counterclockwise outline."));
        return nullptr;
    }
    APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>();
    if (!Volume)
    {
        return nullptr;
    }
    UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume);
    UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
    WandererVolumeBrush::BuildVolumePrism(*Volume, Outline, Depth);
    Volume->SetActorLocation(SurfaceLocation);
    Volume->bWaterVolume = true;
    Volume->bPhysicsOnContact = false;
    Volume->FluidFriction = WaterFluidFriction;
    Volume->Priority = WaterPriority;
    Volume->Tags.Add(TEXT("WaterBody"));
    Volume->MarkPackageDirty();
    return Volume;
}

bool USceneWaterLibrary::SetTrialBodyWaterlineOffset(
    UMaterial* Material, UMaterialExpression* Offset)
{
    if (!IsInGameThread() || !IsValid(Material) || !IsValid(Offset)
        || Offset->GetTypedOuter<UMaterial>() != Material
        || (Material->GetPathName() != TEXT("/Game/Materials/Water/M_BodyWaterline.M_BodyWaterline")
            && Material->GetPathName()
                   != TEXT(
                       "/Game/Materials/Water/M_ContactWaterSurfaceV2.M_ContactWaterSurfaceV2")))
    {
        return false;
    }
    Material->Modify();
    FExpressionInput& Input = Material->GetEditorOnlyData()->WorldPositionOffset;
    Input = FExpressionInput();
    Input.Expression = Offset;
    Material->PostEditChange();
    Material->MarkPackageDirty();
    return true;
}
