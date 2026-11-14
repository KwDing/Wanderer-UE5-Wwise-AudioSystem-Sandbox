#include "Components/CharacterFillLightingComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

#include "Data/CharacterPresentationData.h"
#include "Data/WorldPresentationSettings.h"

namespace
{
    void ConfigureEnvironmentFillReceivers(AActor& Actor)
    {
        // Channel 2 is for camera fill on scenery, never on pawn-owned accessories.
        TInlineComponentArray<UStaticMeshComponent*> Meshes(&Actor);
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            if (!Mesh->LightingChannels.bChannel2)
            {
                Mesh->SetLightingChannels(
                    Mesh->LightingChannels.bChannel0, Mesh->LightingChannels.bChannel1, true);
            }
        }
    }

} // namespace

void UCharacterFillLightingComponent::Initialize(UCameraComponent* InCamera,
    UPointLightComponent* InKey,
    UPointLightComponent* InFill,
    USpotLightComponent* InView)
{
    DisableLights();
    CameraWeakPtr.Reset();
    KeyLightWeakPtr.Reset();
    FillLightWeakPtr.Reset();
    ViewLightWeakPtr.Reset();
    SceneLightsWeakPtr.Reset();
    if (!IsValid(InCamera) || !IsValid(InKey) || !IsValid(InFill) || !IsValid(InView)
        || InCamera->GetOwner() != GetOwner() || InKey->GetOwner() != GetOwner()
        || InFill->GetOwner() != GetOwner() || InView->GetOwner() != GetOwner())
    {
        return;
    }
    CameraWeakPtr = InCamera;
    KeyLightWeakPtr = InKey;
    FillLightWeakPtr = InFill;
    ViewLightWeakPtr = InView;
    SceneLightsWeakPtr.Reset();
    LightRefreshCountdown = 0.0f;
    LightingSampleCountdown = 0.0f;
}

void UCharacterFillLightingComponent::SetEnabled(bool bInEnabled)
{
    bEnabled = bInEnabled;
    LightingSampleCountdown = 0.0f;
    if (!bEnabled)
    {
        DisableLights();
    }
}

void UCharacterFillLightingComponent::Update(
    float InDeltaSeconds, bool bInLocallyControlled, float InCharacterLumens, float InCameraLumens)
{
    if (!bEnabled || !bInLocallyControlled || !IsValid(GetOwner()) || !CameraWeakPtr.IsValid()
        || !KeyLightWeakPtr.IsValid() || !FillLightWeakPtr.IsValid() || !ViewLightWeakPtr.IsValid())
    {
        DisableLights();
        return;
    }
    // Keep a soft key on the visible side through camera orbit, independently of camera distance.
    const FVector CameraSide =
        (CameraWeakPtr.Get()->GetComponentLocation() - GetOwner()->GetActorLocation())
            .GetSafeNormal2D();
    const FVector BodyFacing =
        CameraSide.IsNearlyZero() ? -GetOwner()->GetActorForwardVector() : CameraSide;
    const FVector Side = FVector::CrossProduct(FVector::UpVector, BodyFacing);
    KeyLightWeakPtr->SetWorldLocation(
        GetOwner()->GetActorLocation()
        + BodyFacing * CharacterPresentationSettings::KeyLightOffset.X
        + Side * CharacterPresentationSettings::KeyLightOffset.Y
        + FVector::UpVector * CharacterPresentationSettings::KeyLightOffset.Z);
    FillLightWeakPtr->SetWorldLocation(
        GetOwner()->GetActorLocation()
        + BodyFacing * CharacterPresentationSettings::FillLightOffset.X
        + Side * CharacterPresentationSettings::FillLightOffset.Y
        + FVector::UpVector * CharacterPresentationSettings::FillLightOffset.Z);
    LightRefreshCountdown -= InDeltaSeconds;
    if (LightRefreshCountdown <= 0.0f)
    {
        RefreshSceneLights();
    }
    LightingSampleCountdown -= InDeltaSeconds;
    if (LightingSampleCountdown <= 0.0f)
    {
        UpdateLightingTargets(BodyFacing);
    }
    CharacterFillAmount = FMath::FInterpTo(CharacterFillAmount,
        CharacterFillTarget,
        InDeltaSeconds,
        CharacterPresentationSettings::FillInterpolationSpeed);
    CameraFillAmount = FMath::FInterpTo(CameraFillAmount,
        CameraFillTarget,
        InDeltaSeconds,
        CharacterPresentationSettings::FillInterpolationSpeed);
    KeyLightWeakPtr->SetIntensity(InCharacterLumens * CharacterFillAmount);
    FillLightWeakPtr->SetIntensity(InCharacterLumens
                                   * CharacterPresentationSettings::SecondaryFillRatio
                                   * CharacterFillAmount);
    ViewLightWeakPtr->SetIntensity(InCameraLumens * CameraFillAmount);
    KeyLightWeakPtr->SetVisibility(
        CharacterFillAmount > CharacterPresentationSettings::FillVisibilityThreshold);
    FillLightWeakPtr->SetVisibility(
        CharacterFillAmount > CharacterPresentationSettings::FillVisibilityThreshold);
    ViewLightWeakPtr->SetVisibility(
        CameraFillAmount > CharacterPresentationSettings::FillVisibilityThreshold);
}

void UCharacterFillLightingComponent::DisableLights()
{
    CharacterFillAmount = 0.0f;
    CameraFillAmount = 0.0f;
    if (KeyLightWeakPtr.IsValid())
    {
        KeyLightWeakPtr->SetVisibility(false);
    }
    if (FillLightWeakPtr.IsValid())
    {
        FillLightWeakPtr->SetVisibility(false);
    }
    if (ViewLightWeakPtr.IsValid())
    {
        ViewLightWeakPtr->SetVisibility(false);
    }
}

void UCharacterFillLightingComponent::EndPlay(const EEndPlayReason::Type InReason)
{
    SetEnabled(false);
    CameraWeakPtr.Reset();
    KeyLightWeakPtr.Reset();
    FillLightWeakPtr.Reset();
    ViewLightWeakPtr.Reset();
    SceneLightsWeakPtr.Reset();
    Super::EndPlay(InReason);
}

void UCharacterFillLightingComponent::RefreshSceneLights()
{
    SceneLightsWeakPtr.Reset();
    const UWorldPresentationSettings* Settings = GetDefault<UWorldPresentationSettings>();
    const bool bAutomaticReceivers = UWorldPresentationSettings::MatchesMap(
        GetWorld()->GetMapName(), Settings->AutomaticFillReceiverMaps);
    for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
    {
        // Light actors hide their editor icon in game while their light remains active.
        if (Actor->IsA<APawn>())
        {
            continue;
        }
        if (bAutomaticReceivers || Actor->ActorHasTag(Settings->FillReceiverTag))
        {
            ConfigureEnvironmentFillReceivers(**Actor);
        }
        TInlineComponentArray<ULightComponentBase*> Lights(*Actor);
        for (ULightComponentBase* Light : Lights)
        {
            SceneLightsWeakPtr.Add(Light);
        }
    }
    LightRefreshCountdown = CharacterPresentationSettings::SceneLightRefreshSeconds;
}

bool UCharacterFillLightingComponent::IsLightPathClear(
    const FVector& InStart, const FVector& InEnd) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererFillLighting), true, GetOwner());
    return !GetWorld()->LineTraceTestByChannel(InStart, InEnd, ECC_Visibility, Query);
}

float UCharacterFillLightingComponent::EstimateSceneLighting(
    const FVector& InPosition, const FVector& InSurfaceNormal) const
{
    float Brightness = 0.0f;
    for (const TWeakObjectPtr<ULightComponentBase>& LightWeakPtr : SceneLightsWeakPtr)
    {
        const ULightComponentBase* Light = LightWeakPtr.Get();
        if (!IsValid(Light) || !Light->IsVisible() || !Light->IsRegistered()
            || Light->Intensity <= 0.0f)
        {
            continue;
        }
        if (const UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
        {
            if (Sun->LightingChannels.bChannel0
                && IsLightPathClear(InPosition,
                    InPosition
                        - Sun->GetForwardVector()
                              * CharacterPresentationSettings::SunTraceDistance))
            {
                Brightness += Sun->Intensity / CharacterPresentationSettings::SunIntensityScale;
            }
        }
        else if (const USkyLightComponent* Sky = Cast<USkyLightComponent>(Light))
        {
            // Open sky retains a small amount of fill in shade; enclosed rooms need more.
            if (IsLightPathClear(InPosition,
                    InPosition
                        + FVector(0.0f, 0.0f, CharacterPresentationSettings::SkyTraceDistance)))
            {
                Brightness += Sky->Intensity * CharacterPresentationSettings::SkyIntensityScale;
            }
        }
        else if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
        {
            Brightness += EstimateLocalLighting(*Local, InPosition, InSurfaceNormal);
        }
    }
    return FMath::Clamp(Brightness, 0.0f, 1.0f);
}

float UCharacterFillLightingComponent::EstimateLocalLighting(const ULocalLightComponent& InLight,
    const FVector& InPosition,
    const FVector& InSurfaceNormal) const
{
    const FVector ToSample = InPosition - InLight.GetComponentLocation();
    const float Distance = ToSample.Size();
    if (!InLight.LightingChannels.bChannel0 || Distance >= InLight.AttenuationRadius)
    {
        return 0.0f;
    }
    const USpotLightComponent* Spot = Cast<USpotLightComponent>(&InLight);
    const float ConeCosine = Spot ? Spot->GetCosHalfConeAngle() : -1.0f;
    if (Spot
        && FVector::DotProduct(ToSample.GetSafeNormal(), Spot->GetForwardVector()) < ConeCosine)
    {
        return 0.0f;
    }
    if (!IsLightPathClear(InPosition, InLight.GetComponentLocation()))
    {
        return 0.0f;
    }
    const float Candelas = InLight.Intensity
                           * ULocalLightComponent::GetUnitsConversionFactor(
                               InLight.IntensityUnits, ELightUnits::Candelas, ConeCosine);
    const float RadiusFade = FMath::Square(1.0f
                                           - FMath::Pow(Distance / InLight.AttenuationRadius,
                                               CharacterPresentationSettings::RadiusFadeExponent));
    // A lamp behind the body cannot make its camera-facing side readable.
    const float Facing =
        InSurfaceNormal.IsNearlyZero()
            ? 1.0f
            : FMath::Max(0.0f, FVector::DotProduct(InSurfaceNormal, -ToSample.GetSafeNormal()));
    return Facing * Candelas * RadiusFade
           / (FMath::Max(CharacterPresentationSettings::MinimumSquaredLightDistanceM,
                  FMath::Square(Distance * CharacterPresentationSettings::MetersPerCentimeter))
               * CharacterPresentationSettings::LocalLightIntensityScale);
}

void UCharacterFillLightingComponent::UpdateLightingTargets(const FVector& InBodyFacing)
{
    const FVector BodySample =
        GetOwner()->GetActorLocation() + CharacterPresentationSettings::BodySampleOffset;
    CharacterFillTarget = 1.0f - EstimateSceneLighting(BodySample, InBodyFacing);
    const UCameraComponent* Camera = CameraWeakPtr.Get();
    const FVector Start = Camera->GetComponentLocation();
    const FVector End =
        Start + Camera->GetForwardVector() * CharacterPresentationSettings::ViewTraceDistance;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererViewFill), true, GetOwner());
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    const FVector ViewSample =
        bHit ? Hit.ImpactPoint + Hit.ImpactNormal * CharacterPresentationSettings::ViewSurfaceOffset
             : End;
    CameraFillTarget = 1.0f - EstimateSceneLighting(ViewSample);
    LightingSampleCountdown = CharacterPresentationSettings::LightingSampleSeconds;
}
