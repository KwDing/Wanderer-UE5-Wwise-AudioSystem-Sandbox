#pragma once

#include "CoreMinimal.h"

namespace SceneAuthoringParameters
{
    constexpr float MinDropletScale = 0.03f;
    constexpr float MaxDropletScale = 0.2f;
    constexpr float SwimmingSprayScale = 0.09f;
    constexpr float WaterFluidFriction = 0.3f;
    constexpr int32 WaterPriority = 10;
    constexpr int32 MaxLandscapeComponents = 8;
    constexpr int32 MinLandscapeQuads = 7;
    constexpr int32 MaxLandscapeQuads = 255;
    constexpr int32 MaxFountainSamples = 1024;
    constexpr int32 MaxLakeSamples = 8192;
    constexpr double GridTolerance = 0.01;
    inline const FName TerrainTag(TEXT("TrialProceduralTerrain"));
    inline const FString ScenePackage(TEXT("/Game/Maps/L_AudioSystemDemo"));
    inline const FVector FountainMinimum(-800, -4900, -80);
    inline const FVector FountainMaximum(100, -4100, -25);
    inline const FVector LakeMinimum(-1700, -10300, -400);
    inline const FVector LakeMaximum(1450, -7000, 120);
    inline const TCHAR* const CollisionMeshes[] = {
        TEXT("/Game/Meshes/Props/Fountains/SM_FountainWalkableSteps."
             "SM_FountainWalkableSteps"),
        TEXT("/Game/Meshes/Water/SM_SurfaceSheet_FountainFrontWater."
             "SM_SurfaceSheet_FountainFrontWater"),
        TEXT("/Game/Meshes/Water/SM_SurfaceSheet_FountainRearWater."
             "SM_SurfaceSheet_FountainRearWater"),
        TEXT("/Game/Meshes/Water/SM_SurfaceSheet_Trial_InteractiveLakeSurface."
             "SM_SurfaceSheet_Trial_InteractiveLakeSurface")};
    inline const TCHAR* const DropletMaterials[] = {
        TEXT("/Game/Materials/Ground/Footprints/MI_SandImpression_Left."
             "MI_SandImpression_Left"),
        TEXT("/Game/Materials/Ground/Footprints/MI_SandImpression_Right."
             "MI_SandImpression_Right"),
        TEXT("/Game/Materials/Water/MI_SmallWaterDroplets.MI_SmallWaterDroplets"),
        TEXT("/Game/Materials/Ground/Soil/M_Natural18_ShoreSoil.M_Natural18_ShoreSoil"),
        TEXT("/Game/Materials/Ground/Soil/M_Natural18_ShoreTerrain."
             "M_Natural18_ShoreTerrain"),
        TEXT("/Game/Materials/Ground/Footprints/M_SandImpression.M_SandImpression"),
        TEXT("/Game/Materials/Water/MI_ContactSpray.MI_ContactSpray"),
        TEXT("/Game/Materials/Water/M_ContactCrest_Variant02.M_ContactCrest_Variant02"),
        TEXT("/Game/Materials/Water/MI_ContactSpray_Variant02.MI_ContactSpray_Variant02"),
        TEXT("/Game/Materials/Water/M_BodyWaterline_Variant02.M_BodyWaterline_Variant02"),
        TEXT("/Game/Materials/Ground/Snow/M_SoftSnow_Variant02.M_SoftSnow_Variant02"),
        TEXT("/Game/Materials/Ground/Snow/M_TerrainWithoutSnowPaint."
             "M_TerrainWithoutSnowPaint")};
    inline const TCHAR* const DropletSystems[] = {
        TEXT("/Game/VFX/Niagara/Systems/Water/NS_SmallWaterDroplets."
             "NS_SmallWaterDroplets"),
        TEXT("/Game/VFX/Niagara/Systems/Water/NS_ContactWaterDroplets."
             "NS_ContactWaterDroplets"),
        TEXT("/Game/VFX/Niagara/Systems/Water/NS_ContactSpray.NS_ContactSpray"),
        TEXT("/Game/VFX/Niagara/Systems/Water/NS_ContactSpray_Variant02."
             "NS_ContactSpray_Variant02")};
    inline const TCHAR* const TerrainMaterials[] = {
        TEXT("/Game/Materials/Water/MI_ContactSpray_Variant02.MI_ContactSpray_Variant02"),
        TEXT("/Game/Materials/Water/M_BodyWaterline_Variant02.M_BodyWaterline_Variant02"),
        TEXT("/Game/Materials/Ground/Snow/M_SoftSnow_Variant02.M_SoftSnow_Variant02"),
        TEXT("/Game/Materials/Ground/Snow/M_TerrainWithoutSnowPaint."
             "M_TerrainWithoutSnowPaint"),
        TEXT("/Game/Materials/Ground/Terrain/M_Terrain.M_Terrain"),
        TEXT("/Game/Materials/Ground/Soil/M_WetShore.M_WetShore")};
} // namespace SceneAuthoringParameters
