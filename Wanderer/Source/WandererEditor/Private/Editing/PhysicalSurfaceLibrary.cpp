#include "Editing/PhysicalSurfaceLibrary.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLandscapePhysicalMaterialOutput.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "MeshDescription.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicalMaterials/PhysicalMaterialMask.h"
#include "StaticMeshAttributes.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr int32 MaximumMeshUVChannels = 8;
    constexpr int32 PhysicalMaskPaletteSize = 8;
}

int32 UPhysicalSurfaceLibrary::AddPhysicalMaskUV(
    UStaticMesh* Mesh, const FTransform& LocalToWorld, FVector2D Minimum, FVector2D Size)
{
    if (!IsInGameThread() || !Mesh || Size.ContainsNaN() || Minimum.ContainsNaN()
        || Size.X <= 0 || Size.Y <= 0)
    {
        return INDEX_NONE;
    }
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!Description)
    {
        return INDEX_NONE;
    }
    FStaticMeshAttributes Attributes(*Description);
    auto UVs = Attributes.GetVertexInstanceUVs();
    const int32 Channel = UVs.GetNumChannels();
    if (Channel >= MaximumMeshUVChannels)
    {
        return INDEX_NONE;
    }
    Mesh->Modify();
    UVs.SetNumChannels(Channel + 1);
    const auto Positions = Attributes.GetVertexPositions();
    for (FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
    {
        const FVector Position = LocalToWorld.TransformPosition(
            FVector(Positions[Description->GetVertexInstanceVertex(Instance)]));
        UVs.Set(Instance,
            Channel,
            FVector2f((Position.X - Minimum.X) / Size.X, (Position.Y - Minimum.Y) / Size.Y));
    }
    Mesh->bSupportPhysicalMaterialMasks = true;
    Mesh->CommitMeshDescription(0);
    Mesh->Build(false);
    FPropertyChangedEvent MaskPropertyChanged(FindFProperty<FProperty>(UStaticMesh::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UStaticMesh, bSupportPhysicalMaterialMasks)));
    Mesh->PostEditChangeProperty(MaskPropertyChanged);
    return Channel;
}

bool UPhysicalSurfaceLibrary::ConfigureLandscapePhysicalOutput(UMaterial* Material,
    UTexture2D* Texture,
    const TArray<UPhysicalMaterial*>& Materials,
    FVector2D Minimum,
    FVector2D Size)
{
    if (!IsInGameThread() || !Material || !Texture || Materials.IsEmpty()
        || Size.ContainsNaN() || Minimum.ContainsNaN()
        || Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }
    for (UPhysicalMaterial* Entry : Materials)
    {
        if (!Entry)
        {
            return false;
        }
    }
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression && Expression->IsA<UMaterialExpressionLandscapePhysicalMaterialOutput>())
        {
            return false;
        }
    }
    Material->Modify();
    UMaterialExpressionLandscapePhysicalMaterialOutput* Output =
        NewObject<UMaterialExpressionLandscapePhysicalMaterialOutput>(Material);
    UMaterialExpressionWorldPosition* Position =
        NewObject<UMaterialExpressionWorldPosition>(Material);
    UMaterialExpressionTextureObject* TextureObject =
        NewObject<UMaterialExpressionTextureObject>(Material);
    TextureObject->Texture = Texture;
    TextureObject->SamplerType = SAMPLERTYPE_LinearColor;
    Material->GetExpressionCollection().AddExpression(Output);
    Material->GetExpressionCollection().AddExpression(Position);
    Material->GetExpressionCollection().AddExpression(TextureObject);
    for (int32 Index = 0; Index < Materials.Num(); ++Index)
    {
        UMaterialExpressionCustom* Weight = NewObject<UMaterialExpressionCustom>(Material);
        Weight->OutputType = CMOT_Float1;
        Weight->Code =
            FString::Printf(TEXT("float2 uv=(P.xy-float2(%.9f,%.9f))/float2(%.9f,%.9f); r"
                                 "eturn abs(Texture2DSample(T,TSampler,uv).r*255.0-%d.0)<"
                                 "0.5 ? 1.0 : 0.0;"),
                Minimum.X,
                Minimum.Y,
                Size.X,
                Size.Y,
                Index);
        FCustomInput& PositionInput = Weight->Inputs.AddDefaulted_GetRef();
        PositionInput.InputName = TEXT("P");
        PositionInput.Input.Expression = Position;
        FCustomInput& TextureInput = Weight->Inputs.AddDefaulted_GetRef();
        TextureInput.InputName = TEXT("T");
        TextureInput.Input.Expression = TextureObject;
        Material->GetExpressionCollection().AddExpression(Weight);
        FPhysicalMaterialInput& Input = Output->Inputs.AddDefaulted_GetRef();
        Input.PhysicalMaterial = Materials[Index];
        Input.Input.Expression = Weight;
    }
    Material->PostEditChange();
    return true;
}

bool UPhysicalSurfaceLibrary::ConfigurePhysicalMask(UMaterial* Material,
    UPhysicalMaterialMask* Mask,
    UTexture2D* Texture,
    const TArray<UPhysicalMaterial*>& Materials,
    int32 UVChannel)
{
    if (!IsInGameThread() || !Material || !Mask || !Texture || Materials.IsEmpty()
        || Materials.Num() > PhysicalMaskPaletteSize || UVChannel < 0)
    {
        return false;
    }
    for (UPhysicalMaterial* Entry : Materials)
    {
        if (!Entry)
        {
            return false;
        }
    }
    Mask->Modify();
    Mask->UVChannelIndex = UVChannel;
    Mask->AddressX = TA_Clamp;
    Mask->AddressY = TA_Clamp;
    Mask->SetMaskTexture(Texture, TEXT(""));
    Mask->PostEditChange();
    Material->Modify();
    Material->PhysMaterialMask = Mask;
    Material->PhysMaterial = Materials[0];
    for (int32 Index = 0; Index < PhysicalMaskPaletteSize; ++Index)
    {
        Material->PhysicalMaterialMap[Index] =
            Materials.IsValidIndex(Index) ? Materials[Index] : Materials[0];
    }
    Material->PostEditChange();
    return true;
}

void UPhysicalSurfaceLibrary::RefreshNativeSurfaceCollision(AActor* Actor)
{
    if (!IsInGameThread() || !Actor)
    {
        return;
    }
    TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
    for (UPrimitiveComponent* Component : Components)
    {
        Component->RecreatePhysicsState();
    }
}

bool UPhysicalSurfaceLibrary::SetNativeSurfaceType(UPhysicalMaterial* Material, int32 SurfaceType)
{
    if (!IsInGameThread() || !Material || SurfaceType < 0 || SurfaceType >= SurfaceType_Max)
    {
        return false;
    }
    Material->Modify();
    Material->SurfaceType = static_cast<EPhysicalSurface>(SurfaceType);
    Material->PostEditChange();
    return true;
}

bool UPhysicalSurfaceLibrary::UpdateLandscapePhysicalSurfaces(AActor* LandscapeActor)
{
    ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(LandscapeActor);
    if (!IsInGameThread() || !Landscape)
    {
        return false;
    }
    Landscape->BuildPhysicalMaterial();
    return Landscape->GetOudatedPhysicalMaterialComponentsCount() == 0;
}
