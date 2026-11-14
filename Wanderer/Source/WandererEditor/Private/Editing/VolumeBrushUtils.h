#pragma once

#include "CoreMinimal.h"

class AVolume;
class UModel;

// Synchronous editor brush operations shared by acoustics, water and PCG volumes.
namespace WandererVolumeBrush
{
    bool IsValidVolumeOutline(const TArray<FVector2D>& Outline, float Depth);
    void AddFace(UModel& Model, const TArray<FVector3f>& Vertices);
    void BuildVolumePrism(
        AVolume& Volume, const TArray<FVector2D>& Outline, float Depth, float TopZ = 0.0f);
} // namespace WandererVolumeBrush
