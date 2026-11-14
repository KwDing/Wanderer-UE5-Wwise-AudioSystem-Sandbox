#include "Core/SurfaceQueryLibrary.h"

#include "PhysicalMaterials/PhysicalMaterial.h"

int32 USurfaceQueryLibrary::GetNativeSurfaceType(UPhysicalMaterial* Material)
{
    return Material ? static_cast<int32>(Material->SurfaceType.GetValue()) : 0;
}
