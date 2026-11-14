#include "Features/SceneAuthoring/SceneAssetLibrary.h"

#include "ActorFactories/ActorFactory.h"
#include "AssetCompilingManager.h"
#include "Builders/CubeBuilder.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/ArchiveReplaceObjectRef.h"

#include "PCGVolume.h"

#include "Editing/VolumeBrushUtils.h"
#include "Features/SceneAuthoring/SceneAuthoringData.h"
#include "Features/SceneAuthoring/SceneAuthoringUtils.h"

namespace
{
    using namespace SceneAuthoringParameters;
}

bool USceneAssetLibrary::ConfigureTrialStepCollision(
    UStaticMesh* Mesh, EWandererStepCollisionMode Mode)
{
    if (!IsInGameThread() || !SceneAuthoringUtils::IsAllowedAsset(Mesh, CollisionMeshes))
    {
        return false;
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    UBodySetup* Body = Mesh->GetBodySetup();
    if (!Body)
    {
        return false;
    }
    Mesh->Modify();
    Body->Modify();
    Body->CollisionTraceFlag =
        Mode == EWandererStepCollisionMode::ModelSurface ? CTF_UseComplexAsSimple : CTF_UseDefault;
    Body->bDoubleSidedGeometry = true;
    Body->InvalidatePhysicsData();
    Body->CreatePhysicsMeshes();
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    return true;
}

AActor* USceneAssetLibrary::CreatePCGVolume(UObject* WorldContext,
    const TArray<FVector2D>& Outline,
    const FVector& TopLocation,
    float Depth)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor || TopLocation.ContainsNaN()
        || !WandererVolumeBrush::IsValidVolumeOutline(Outline, Depth))
    {
        return nullptr;
    }
    APCGVolume* Volume = World->SpawnActor<APCGVolume>();
    if (!Volume)
    {
        return nullptr;
    }
    UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume);
    UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
    WandererVolumeBrush::BuildVolumePrism(*Volume, Outline, Depth);
    Volume->SetActorLocation(TopLocation);
    Volume->SetActorLabel(TEXT("PCG_DenseGrass"));
    Volume->SetFolderPath(TEXT("ProceduralTerrain"));
    Volume->Tags.Add(TEXT("TrialPCGGrass"));
    Volume->MarkPackageDirty();
    return Volume;
}

bool USceneAssetLibrary::ReplaceSceneAssetReferences(
    const TArray<UObject*>& SearchObjects, const TMap<UObject*, UObject*>& Replacements)
{
    const FString& SceneRoot = ScenePackage;
    if (!IsInGameThread() || SearchObjects.IsEmpty())
    {
        return false;
    }
    for (UObject* Object : SearchObjects)
    {
        if (!IsValid(Object))
        {
            return false;
        }
        const FString PackageName = Object->GetOutermost()->GetName();
        if (PackageName != SceneRoot && !PackageName.StartsWith(SceneRoot + TEXT("/")))
        {
            return false;
        }
        if (const UWorld* World = Cast<UWorld>(Object);
            World && World->WorldType != EWorldType::Editor)
        {
            return false;
        }
    }
    for (const TPair<UObject*, UObject*>& Pair : Replacements)
    {
        if (!IsValid(Pair.Key) || !IsValid(Pair.Value)
            || !Pair.Value->GetPathName().StartsWith(SceneRoot))
        {
            return false;
        }
    }
    for (UObject* Object : SearchObjects)
    {
        FArchiveReplaceObjectRef<UObject> Archive(Object,
            Replacements,
            EArchiveReplaceObjectFlags::IgnoreOuterRef
                | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
        Object->MarkPackageDirty();
        Object->PostEditChange();
    }
    return true;
}
