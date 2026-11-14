#include "Features/SceneAuthoring/SceneTerrainLibrary.h"

#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "Materials/MaterialInterface.h"

#include "Features/SceneAuthoring/SceneAuthoringData.h"
#include "Features/SceneAuthoring/SceneAuthoringUtils.h"
#include "Features/SceneAuthoring/SceneTerrainUtils.h"
#include "Foundation/AuthoringLog.h"

namespace
{
    using namespace SceneAuthoringParameters;

    bool GatherFountainTargets(const FTransform& InTransform,
        const TArray<FVector>& InSamples,
        const FIntRect& InExtent,
        TMap<FIntPoint, uint16>& OutTargets)
    {
        for (const FVector& Sample : InSamples)
        {
            if (Sample.ContainsNaN() || Sample.X < FountainMinimum.X || Sample.X > FountainMaximum.X
                || Sample.Y < FountainMinimum.Y || Sample.Y > FountainMaximum.Y
                || Sample.Z < FountainMinimum.Z || Sample.Z > FountainMaximum.Z)
            {
                return false;
            }
            const FVector Local = InTransform.InverseTransformPosition(Sample);
            const FIntPoint Grid(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y));
            if (Grid.X < InExtent.Min.X || Grid.X > InExtent.Max.X || Grid.Y < InExtent.Min.Y
                || Grid.Y > InExtent.Max.Y || FMath::Abs(Local.X - Grid.X) > GridTolerance
                || FMath::Abs(Local.Y - Grid.Y) > GridTolerance)
            {
                return false;
            }
            OutTargets.Add(Grid, LandscapeDataAccess::GetTexHeight(Local.Z));
        }
        return true;
    }

    bool GatherLakeTargets(const FTransform& InTransform,
        const TArray<FVector>& InSamples,
        const FIntRect& InExtent,
        TMap<FIntPoint, uint16>& OutTargets,
        FIntRect& OutBounds)
    {
        for (const FVector& Sample : InSamples)
        {
            if (Sample.ContainsNaN() || Sample.X < LakeMinimum.X || Sample.X > LakeMaximum.X
                || Sample.Y < LakeMinimum.Y || Sample.Y > LakeMaximum.Y || Sample.Z < LakeMinimum.Z
                || Sample.Z > LakeMaximum.Z)
            {
                return false;
            }
            const FVector Local = InTransform.InverseTransformPosition(Sample);
            const FIntPoint Grid(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y));
            if (Grid.X < InExtent.Min.X || Grid.X > InExtent.Max.X || Grid.Y < InExtent.Min.Y
                || Grid.Y > InExtent.Max.Y || FMath::Abs(Local.X - Grid.X) > GridTolerance
                || FMath::Abs(Local.Y - Grid.Y) > GridTolerance || OutTargets.Contains(Grid))
            {
                return false;
            }
            OutBounds.Min.X = FMath::Min(OutBounds.Min.X, Grid.X);
            OutBounds.Min.Y = FMath::Min(OutBounds.Min.Y, Grid.Y);
            OutBounds.Max.X = FMath::Max(OutBounds.Max.X, Grid.X);
            OutBounds.Max.Y = FMath::Max(OutBounds.Max.Y, Grid.Y);
            OutTargets.Add(Grid, LandscapeDataAccess::GetTexHeight(Local.Z));
        }
        return true;
    }

    void ImportTerrain(ALandscape& InLandscape,
        UMaterialInterface* InMaterial,
        const FSceneTerrainGrid& InGrid,
        int32 InQuads,
        TArray<uint16>& InOutHeights)
    {
        InLandscape.bCanHaveLayersContent = true;
        InLandscape.LandscapeMaterial = InMaterial;
        InLandscape.SetActorRelativeScale3D(FVector(InGrid.XYScale, InGrid.XYScale, InGrid.ZScale));
        TMap<FGuid, TArray<uint16>> HeightData;
        TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
        HeightData.Add(FGuid(), MoveTemp(InOutHeights));
        LayerData.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
        InLandscape.Import(FGuid::NewGuid(),
            0,
            0,
            InGrid.Size.X - 1,
            InGrid.Size.Y - 1,
            1,
            InQuads,
            HeightData,
            nullptr,
            LayerData,
            ELandscapeImportAlphamapType::Additive,
            TArrayView<const FLandscapeLayer>());
    }
    bool LowerTerrainSamples(ALandscape& InLandscape,
        ULandscapeInfo& InInfo,
        const FIntRect& InExtent,
        TMap<FIntPoint, uint16>& InOutTargets)
    {
        const FGuid Layer =
            InLandscape.GetLayers().IsEmpty() ? FGuid() : InLandscape.GetLayers()[0].Guid;
        InLandscape.Modify();
        {
            FScopedSetLandscapeEditingLayer Editing(&InLandscape, Layer);
            FHeightmapAccessor<false> Accessor(&InInfo);
            Accessor.SetEditLayer(Layer);
            TMap<FIntPoint, uint16> Original;
            Accessor.GetDataFast(
                InExtent.Min.X, InExtent.Min.Y, InExtent.Max.X, InExtent.Max.Y, Original);
            for (TPair<FIntPoint, uint16>& Target : InOutTargets)
            {
                const uint16* Previous = Original.Find(Target.Key);
                if (!Previous)
                {
                    return false;
                }
                Target.Value = FMath::Min(*Previous, Target.Value);
            }
            for (const TPair<FIntPoint, uint16>& Target : InOutTargets)
            {
                Accessor.SetData(
                    Target.Key.X, Target.Key.Y, Target.Key.X, Target.Key.Y, &Target.Value);
            }
        }
        return true;
    }
    TArray<uint16> CollectLakeHeights(
        const FIntRect& InBounds, const TMap<FIntPoint, uint16>& InTargets)
    {
        TArray<uint16> Heights;
        Heights.Reserve(InTargets.Num());
        for (int32 Y = InBounds.Min.Y; Y <= InBounds.Max.Y; ++Y)
        {
            for (int32 X = InBounds.Min.X; X <= InBounds.Max.X; ++X)
            {
                Heights.Add(InTargets.FindChecked(FIntPoint(X, Y)));
            }
        }
        return Heights;
    }
} // namespace

AActor* USceneTerrainLibrary::CreateProceduralLandscape(UObject* WorldContext,
    UMaterialInterface* Material,
    const FVector& Origin,
    int32 ComponentCountX,
    int32 ComponentCountY,
    int32 QuadsPerSection,
    float XYScale,
    float ZScale)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor || !Material || Origin.ContainsNaN()
        || ComponentCountX < 1 || ComponentCountX > MaxLandscapeComponents || ComponentCountY < 1
        || ComponentCountY > MaxLandscapeComponents
        || !FMath::IsWithinInclusive(QuadsPerSection, MinLandscapeQuads, MaxLandscapeQuads)
        || XYScale <= 0.0f || ZScale <= 0.0f)
    {
        UE_LOG(LogWandererAuthoring,
            Warning,
            TEXT("Procedural landscape requires an editor world, material"
                 ", finite origin and bounded dimensions."));
        return nullptr;
    }
    const int32 SizeX = ComponentCountX * QuadsPerSection + 1;
    const int32 SizeY = ComponentCountY * QuadsPerSection + 1;
    TArray<uint16> Heights =
        SceneTerrainUtils::BuildHeights({Origin, FIntPoint(SizeX, SizeY), XYScale, ZScale});
    ALandscape* Landscape = World->SpawnActor<ALandscape>(Origin, FRotator::ZeroRotator);
    if (!Landscape)
    {
        return nullptr;
    }
    ImportTerrain(*Landscape,
        Material,
        {Origin, FIntPoint(SizeX, SizeY), XYScale, ZScale},
        QuadsPerSection,
        Heights);
    if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
    {
        Info->UpdateLayerInfoMap(Landscape);
    }
    Landscape->SetActorLabel(TEXT("ProceduralLandscape"));
    Landscape->SetFolderPath(TEXT("ProceduralTerrain"));
    Landscape->Tags.Add(TerrainTag);
    Landscape->MarkPackageDirty();
    Landscape->PostEditChange();
    return Landscape;
}

bool USceneTerrainLibrary::LowerTrialFountainTerrain(
    AActor* Terrain, const TArray<FVector>& WorldSamples)
{
    ALandscape* Landscape = Cast<ALandscape>(Terrain);
    if (!IsInGameThread() || !IsValid(Landscape) || !Landscape->ActorHasTag(TerrainTag)
        || WorldSamples.IsEmpty() || WorldSamples.Num() > MaxFountainSamples
        || Landscape->GetLayers().Num() > 1)
    {
        return false;
    }
    ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
    if (!Info)
    {
        return false;
    }
    int32 MinX;
    int32 MinY;
    int32 MaxX;
    int32 MaxY;
    if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
    {
        return false;
    }
    const FTransform Transform = Landscape->LandscapeActorToWorld();
    TMap<FIntPoint, uint16> Targets;
    if (!GatherFountainTargets(Transform, WorldSamples, FIntRect(MinX, MinY, MaxX, MaxY), Targets))
    {
        return false;
    }
    if (!LowerTerrainSamples(*Landscape, *Info, FIntRect(MinX, MinY, MaxX, MaxY), Targets))
    {
        return false;
    }
    Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All, true);
    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();
    return true;
}

bool USceneTerrainLibrary::RefreshTrialTerrainMaterial(
    AActor* Terrain, UMaterialInterface* Material)
{
    if (!IsInGameThread())
    {
        return false;
    }
    ALandscape* Landscape = Cast<ALandscape>(Terrain);
    if (!IsValid(Landscape) || !Landscape->GetWorld()
        || Landscape->GetWorld()->WorldType != EWorldType::Editor
        || Landscape->GetOutermost()->GetName() != ScenePackage
        || !Landscape->ActorHasTag(TerrainTag)
        || !SceneAuthoringUtils::IsAllowedAsset(Material, TerrainMaterials))
    {
        return false;
    }
    Landscape->Modify();
    Landscape->LandscapeMaterial = Material;
    Landscape->UpdateAllComponentMaterialInstances(true);
    Landscape->MarkPackageDirty();
    return true;
}

bool USceneTerrainLibrary::RegradeTrialLakeTerrain(
    AActor* Terrain, const TArray<FVector>& WorldSamples)
{
    if (!IsInGameThread())
    {
        return false;
    }
    ALandscape* Landscape = Cast<ALandscape>(Terrain);
    if (!IsValid(Landscape) || !Landscape->ActorHasTag(TerrainTag)
        || Landscape->GetOutermost()->GetName() != ScenePackage || WorldSamples.IsEmpty()
        || WorldSamples.Num() > MaxLakeSamples || Landscape->GetLayers().Num() > 1)
    {
        return false;
    }
    ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
    if (!Info)
    {
        return false;
    }
    int32 MinX;
    int32 MinY;
    int32 MaxX;
    int32 MaxY;
    if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
    {
        return false;
    }
    const FTransform Transform = Landscape->LandscapeActorToWorld();
    TMap<FIntPoint, uint16> Targets;
    FIntRect Bounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
    if (!GatherLakeTargets(
            Transform, WorldSamples, FIntRect(MinX, MinY, MaxX, MaxY), Targets, Bounds))
    {
        return false;
    }
    const int32 Width = Bounds.Width() + 1;
    const int32 Height = Bounds.Height() + 1;
    if (Width * Height != Targets.Num())
    {
        return false;
    }
    TArray<uint16> Heights = CollectLakeHeights(Bounds, Targets);
    const FGuid Layer = Landscape->GetLayers().IsEmpty() ? FGuid() : Landscape->GetLayers()[0].Guid;
    Landscape->Modify();
    {
        FScopedSetLandscapeEditingLayer Editing(Landscape, Layer);
        FHeightmapAccessor<false> Accessor(Info);
        Accessor.SetEditLayer(Layer);
        Accessor.SetData(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y, Heights.GetData());
    }
    Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All, true);
    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();
    return true;
}
