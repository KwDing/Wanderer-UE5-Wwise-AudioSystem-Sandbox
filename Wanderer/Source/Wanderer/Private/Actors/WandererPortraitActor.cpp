#include "Actors/WandererPortraitActor.h"

#include "Data/WandererCharacterCatalog.h"
#include "Interfaces/CharacterPresentationProvider.h"
#include "GameFramework/Pawn.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"

AWandererPortraitActor::AWandererPortraitActor()
{
    PrimaryActorTick.bCanEverTick = false;
    PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
    SetRootComponent(PreviewMesh);
    PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMesh->SetComponentTickEnabled(false);
    PreviewMesh->SetVisibleInSceneCaptureOnly(true);
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PortraitCapture"));
    Capture->SetupAttachment(RootComponent);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
    Capture->SetComponentTickEnabled(false);
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetMotionBlur(false);
    KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PortraitKeyLight"));
    KeyLight->SetupAttachment(RootComponent);
    KeyLight->SetIntensity(24000.0f);
    KeyLight->SetAttenuationRadius(1500.0f);
    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PortraitFillLight"));
    FillLight->SetupAttachment(RootComponent);
    FillLight->SetIntensity(12000.0f);
    FillLight->SetAttenuationRadius(1500.0f);
}

bool AWandererPortraitActor::ShowCharacter(const FWandererCharacterEntry& Entry)
{
    if (!Entry.CharacterClass)
    {
        return false;
    }
    const ICharacterPresentationProvider* CharacterTemplate =
        Cast<ICharacterPresentationProvider>(Entry.CharacterClass->GetDefaultObject());
    const USkeletalMeshComponent* Template =
        CharacterTemplate ? CharacterTemplate->GetPresentationMesh() : nullptr;
    if (!Template || !Template->GetSkeletalMeshAsset())
    {
        return false;
    }
    if (!Portrait)
    {
        Portrait = NewObject<UTextureRenderTarget2D>(this);
        Portrait->ClearColor = FLinearColor(0.025f, 0.035f, 0.05f);
        Portrait->InitAutoFormat(768, 1024);
        Capture->TextureTarget = Portrait;
    }
    PreviewMesh->SetSkeletalMeshAsset(Template->GetSkeletalMeshAsset());
    PreviewMesh->SetWorldRotation(Template->GetRelativeRotation());
    PreviewMesh->SetWorldScale3D(Template->GetRelativeScale3D());
    PreviewMesh->EmptyOverrideMaterials();
    for (int32 Index = 0; Index < Template->GetNumMaterials(); ++Index)
    {
        PreviewMesh->SetMaterial(Index, Template->GetMaterial(Index));
    }
    PreviewMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    PreviewMesh->SetAnimation(Entry.PortraitAnimation);
    PreviewMesh->SetPosition(0.1f, false);
    PreviewMesh->TickAnimation(0.0f, false);
    PreviewMesh->RefreshBoneTransforms();
    PreviewMesh->UpdateBounds();
    const FVector Center = PreviewMesh->Bounds.Origin;
    Capture->SetWorldLocationAndRotation(
        Center + FVector(400.0f, 0.0f, 0.0f), FRotator(0.0f, 180.0f, 0.0f));
    Capture->OrthoWidth = FMath::Max(150.0f, PreviewMesh->Bounds.BoxExtent.Z * 1.75f);
    KeyLight->SetWorldLocation(Center + FVector(200.0f, -150.0f, 180.0f));
    FillLight->SetWorldLocation(Center + FVector(150.0f, 150.0f, 80.0f));
    Capture->ShowOnlyComponents.Empty();
    Capture->ShowOnlyComponent(PreviewMesh);
    SetAppearanceParts(CharacterTemplate->GetAppearanceParts());
    SetPreviewActive(true);
    // The enabled capture tick renders the updated portrait, including while the selection UI
    // pauses the world.
    return true;
}

void AWandererPortraitActor::SetPreviewActive(bool bActive)
{
    // Refresh while UI is visible so deferred material/render updates do not leave
    // the portrait stuck on the first frame. Hidden UI incurs no capture ticks.
    Capture->bCaptureEveryFrame = bActive;
    Capture->SetComponentTickEnabled(bActive);
}

UTextureRenderTarget2D* AWandererPortraitActor::ShowPreview(
    UClass* InClass, UAnimationAsset* InAnimation)
{
    FWandererCharacterEntry Entry;
    Entry.CharacterClass = InClass;
    Entry.PortraitAnimation = InAnimation;
    return ShowCharacter(Entry) ? Portrait.Get() : nullptr;
}

void AWandererPortraitActor::EndPlay(const EEndPlayReason::Type InReason)
{
    SetPreviewActive(false);
    Capture->TextureTarget = nullptr;
    Portrait = nullptr;
    Super::EndPlay(InReason);
}

void AWandererPortraitActor::SetAppearanceParts(const TArray<FWandererAppearancePart>& Parts)
{
    for (USkeletalMeshComponent* Component : AppearanceComponents)
    {
        if (IsValid(Component))
        {
            Component->DestroyComponent();
        }
    }
    AppearanceComponents.Reset();
    for (const FWandererAppearancePart& Part : Parts)
    {
        if (!Part.Mesh)
        {
            continue;
        }
        USkeletalMeshComponent* Component =
            NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
        AppearanceComponents.Add(Component);
        Component->SetupAttachment(PreviewMesh);
        Component->SetRelativeTransform(Part.RelativeTransform);
        Component->SetSkeletalMeshAsset(Part.Mesh);
        for (int32 Index = 0; Index < Part.Materials.Num(); ++Index)
        {
            Component->SetMaterial(Index, Part.Materials[Index]);
        }
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetVisibleInSceneCaptureOnly(true);
        Component->SetLeaderPoseComponent(PreviewMesh);
        Component->RegisterComponent();
        Capture->ShowOnlyComponent(Component);
    }
}
