#include "Actors/WandererSpeakerPreviewActor.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"

AWandererSpeakerPreviewActor::AWandererSpeakerPreviewActor()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StudioRoot")));
    for (int32 Index = 0; Index < 2; ++Index)
    {
        UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(
            *FString::Printf(TEXT("ItemMesh%d"), Index));
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetVisibleInSceneCaptureOnly(true);
        Mesh->SetCastShadow(false);
        Mesh->SetLightingChannels(false, true, false);
        Meshes.Add(Mesh);
        USceneCaptureComponent2D* Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(
            *FString::Printf(TEXT("ItemCapture%d"), Index));
        Capture->SetupAttachment(RootComponent);
        Capture->ProjectionType = ECameraProjectionMode::Orthographic;
        Capture->OrthoWidth = 155;
        Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->SetComponentTickEnabled(false);
        Capture->ShowFlags.SetAtmosphere(false);
        Capture->ShowFlags.SetFog(false);
        Capture->ShowFlags.SetMotionBlur(false);
        Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
        Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
        Capture->PostProcessSettings.AutoExposureBias = 0;
        Captures.Add(Capture);
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(
            *FString::Printf(TEXT("ItemStudioLight%d"), Index));
        Light->SetupAttachment(RootComponent);
        Light->SetRelativeLocation(Index == 0 ? FVector(200, -150, 200) : FVector(150, 450, 100));
        Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->SetIntensity(Index == 0 ? 6000 : 3000);
        Light->SetAttenuationRadius(1200);
        Light->SetCastShadows(false);
        Light->SetLightingChannels(false, true, false);
    }
}

UTextureRenderTarget2D* AWandererSpeakerPreviewActor::GetPreviewTexture(
    EWandererSpeakerItem Item) const
{
    const int32 Index = Item == EWandererSpeakerItem::Radio ? 0 : 1;
    return Textures.IsValidIndex(Index) ? Textures[Index] : nullptr;
}

bool AWandererSpeakerPreviewActor::ShowItems(UStaticMesh* InRadioMesh, UStaticMesh* InMountedMesh)
{
    UStaticMesh* Assets[] = {InRadioMesh, InMountedMesh};
    if (!Assets[0] || !Assets[1])
    {
        return false;
    }
    Textures.Reset();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FBoxSphereBounds Bounds = Assets[Index]->GetBounds();
        if (Bounds.BoxExtent.GetMax() <= UE_SMALL_NUMBER)
        {
            return false;
        }
        const double Scale = 100 / (Bounds.BoxExtent.GetMax() * 2);
        const FRotator Facing(0, -90, 0);
        const FVector Offset(0, Index * 300, 0);
        Meshes[Index]->SetStaticMesh(Assets[Index]);
        Meshes[Index]->SetRelativeRotation(Facing);
        Meshes[Index]->SetRelativeScale3D(FVector(Scale));
        Meshes[Index]->SetRelativeLocation(Offset - Facing.RotateVector(Bounds.Origin) * Scale);
        UTextureRenderTarget2D* Texture = NewObject<UTextureRenderTarget2D>(this);
        Texture->ClearColor = FLinearColor(0.018f, 0.024f, 0.035f);
        Texture->InitAutoFormat(256, 256);
        Textures.Add(Texture);
        USceneCaptureComponent2D* Capture = Captures[Index];
        Capture->TextureTarget = Texture;
        const FVector View(250, -180, 110);
        Capture->SetRelativeLocation(Offset + View);
        Capture->SetRelativeRotation((-View).Rotation());
        Capture->ShowOnlyComponents.Empty();
        Capture->ShowOnlyComponent(Meshes[Index]);
        Capture->bCaptureEveryFrame = true;
        Capture->SetComponentTickEnabled(true);
    }
    return true;
}
