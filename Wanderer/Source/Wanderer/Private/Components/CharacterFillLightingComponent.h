#pragma once

#include "Components/ActorComponent.h"

#include "CharacterFillLightingComponent.generated.h"

class UCameraComponent;
class ULightComponentBase;
class ULocalLightComponent;
class UPointLightComponent;
class USpotLightComponent;

/** Pawn-owned adaptive fill state; actor-created camera/lights retain their original ownership. */
UCLASS()
class UCharacterFillLightingComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    void Initialize(UCameraComponent* InCamera,
        UPointLightComponent* InKey,
        UPointLightComponent* InFill,
        USpotLightComponent* InView);
    void SetEnabled(bool bInEnabled);
    void Update(float InDeltaSeconds,
        bool bInLocallyControlled,
        float InCharacterLumens,
        float InCameraLumens);
    void DisableLights();

  protected:
    virtual void EndPlay(const EEndPlayReason::Type InReason) override;

  private:
    void RefreshSceneLights();
    bool IsLightPathClear(const FVector& InStart, const FVector& InEnd) const;
    float EstimateSceneLighting(
        const FVector& InPosition, const FVector& InSurfaceNormal = FVector::ZeroVector) const;

    float EstimateLocalLighting(const ULocalLightComponent& InLight,
        const FVector& InPosition,
        const FVector& InSurfaceNormal) const;
    void UpdateLightingTargets(const FVector& InBodyFacing);

    TWeakObjectPtr<UCameraComponent> CameraWeakPtr;
    TWeakObjectPtr<UPointLightComponent> KeyLightWeakPtr;
    TWeakObjectPtr<UPointLightComponent> FillLightWeakPtr;
    TWeakObjectPtr<USpotLightComponent> ViewLightWeakPtr;
    TArray<TWeakObjectPtr<ULightComponentBase>> SceneLightsWeakPtr;
    bool bEnabled = false;
    float LightingSampleCountdown = 0.0f;
    float LightRefreshCountdown = 0.0f;
    float CharacterFillTarget = 0.0f;
    float CameraFillTarget = 0.0f;
    float CharacterFillAmount = 0.0f;
    float CameraFillAmount = 0.0f;
};
