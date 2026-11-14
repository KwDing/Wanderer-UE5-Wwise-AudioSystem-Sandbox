#include "Characters/WandererPlayableCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "AkComponent.h"

#include "Components/CharacterCameraZoomComponent.h"
#include "Components/CharacterFillLightingComponent.h"
#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"
#include "Components/Footsteps/FootContactComponent.h"
#include "Components/Footsteps/WandererFootstepVFXComponent.h"
#include "Components/SurfaceInteraction/WandererNaturalInteractionComponent.h"
#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

namespace
{
    void ConfigureCharacterLight(UPointLightComponent& Light, const FVector& Position, float Lumens)
    {
        Light.SetMobility(EComponentMobility::Movable);
        Light.SetRelativeLocation(Position);
        Light.SetIntensityUnits(ELightUnits::Lumens);
        Light.SetIntensity(Lumens);
        Light.SetAttenuationRadius(650.0f);
        Light.SetSourceRadius(50.0f);
        Light.SetSoftSourceRadius(80.0f);
        Light.SetCastShadows(false);
        Light.SetLightingChannels(false, true, false);
        Light.SetIndirectLightingIntensity(0.0f);
        Light.SetVolumetricScatteringIntensity(0.0f);
        Light.SetSpecularScale(0.05f);
    }
} // namespace

AWandererPlayableCharacter::AWandererPlayableCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    CameraZoom = CreateDefaultSubobject<UCharacterCameraZoomComponent>(TEXT("CameraZoom"));
    FillLighting = CreateDefaultSubobject<UCharacterFillLightingComponent>(TEXT("FillLighting"));
    GetCapsuleComponent()->bReturnMaterialOnMove = true;
    NaturalInteraction =
        CreateDefaultSubobject<UWandererNaturalInteractionComponent>(TEXT("NaturalInteraction"));
    WaterInteraction =
        CreateDefaultSubobject<UWandererWaterInteractionComponent>(TEXT("WaterInteraction"));
    FootstepVFX = CreateDefaultSubobject<UWandererFootstepVFXComponent>(TEXT("FootstepVFX"));
    FootContacts = CreateDefaultSubobject<UFootContactComponent>(TEXT("FootContacts"));
    GetCharacterMovement()->MaxSwimSpeed = WalkingSpeed;
    GetCharacterMovement()->Buoyancy = 1.15f;
    GetCharacterMovement()->MaxWalkSpeed = WalkingSpeed;
    CharacterKeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CharacterKeyLight"));
    CharacterKeyLight->SetupAttachment(RootComponent);
    ConfigureCharacterLight(*CharacterKeyLight, FVector(180.0f, -100.0f, 100.0f), 120.0f);
    CharacterKeyLight->SetLightColor(FLinearColor(1.0f, 0.92f, 0.82f));
    CharacterFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CharacterFillLight"));
    CharacterFillLight->SetupAttachment(RootComponent);
    ConfigureCharacterLight(*CharacterFillLight, FVector(-150.0f, 100.0f, 50.0f), 60.0f);
    CharacterFillLight->SetLightColor(FLinearColor(0.86f, 0.92f, 1.0f));
    CameraViewLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("CameraViewLight"));
    CameraViewLight->SetupAttachment(GetFollowCamera());
    CameraViewLight->SetMobility(EComponentMobility::Movable);
    CameraViewLight->SetRelativeLocation(FVector(20.0f, 0.0f, 0.0f));
    CameraViewLight->SetIntensityUnits(ELightUnits::Lumens);
    CameraViewLight->SetIntensity(0.0f);
    CameraViewLight->SetLightingChannels(false, false, true);
    CameraViewLight->SetAttenuationRadius(1500.0f);
    CameraViewLight->SetInnerConeAngle(25.0f);
    CameraViewLight->SetOuterConeAngle(50.0f);
    CameraViewLight->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f));
    CameraViewLight->SetCastShadows(true);
    CameraViewLight->SetIndirectLightingIntensity(0.0f);
    CameraViewLight->SetVolumetricScatteringIntensity(0.0f);
    CameraViewLight->SetSpecularScale(0.05f);
    CharacterKeyLight->SetVisibility(false);
    CharacterFillLight->SetVisibility(false);
    CameraViewLight->SetVisibility(false);
    SetCharacterFillLightingEnabled(false);

    // Audio
    FootstepAudio = CreateDefaultSubobject<UAkComponent>(TEXT("FootstepAudio"));
    FootstepAudio->SetupAttachment(RootComponent);
    FootstepAudio->StopWhenOwnerDestroyed = true;
    CharacterAudio =
        CreateDefaultSubobject<UWandererCharacterAudioComponent>(TEXT("CharacterAudio"));
}

void AWandererPlayableCharacter::SetCharacterFillLightingEnabled(bool bEnabled)
{
    FillLighting->SetEnabled(bEnabled);
}

void AWandererPlayableCharacter::BeginPlay()
{
    Super::BeginPlay();
    // Native Wwise Rooms/Portals own propagation; avoid an additional binary UE ray occlusion.
    FootstepAudio->OcclusionRefreshInterval = 0;
    FootstepAudio->bUseReverbVolumes = true;
    CharacterAudio->InitializeAudio(FootstepAudio.Get());
    ConfigureCharacterLightingChannels();
    CameraViewLight->SetLightingChannels(false, false, true);
    FillLighting->Initialize(
        GetFollowCamera(), CharacterKeyLight, CharacterFillLight, CameraViewLight);
    const FString MapName = GetWorld()->GetMapName();
    SetCharacterFillLightingEnabled(MapName.EndsWith(TEXT("L_AudioSystemDemo")));
    InitializeCameraZoom();
    StopRunning();
}

void AWandererPlayableCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateSwimmingInput();
    UpdateSwimmingSurface(DeltaSeconds);
    FillLighting->Update(
        DeltaSeconds, IsLocallyControlled(), MaximumCharacterFillLumens, MaximumCameraFillLumens);
    RefreshCameraZoomConfig();
    CameraZoom->Update(DeltaSeconds);
}

void AWandererPlayableCharacter::UnPossessed()
{
    StopRunning();
    bAudioWasSwimming = false;
    if (FootContacts)
    {
        FootContacts->ResetFootstepCadence();
    }
    if (WaterInteraction)
    {
        WaterInteraction->ClearWaterRegistration();
    }
    FillLighting->DisableLights();
    if (CharacterAudio)
    {
        CharacterAudio->StopPlayback();
    }
    Super::UnPossessed();
}
