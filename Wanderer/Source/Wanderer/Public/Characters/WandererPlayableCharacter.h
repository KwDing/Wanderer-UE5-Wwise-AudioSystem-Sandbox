#pragma once

#include "CoreMinimal.h"

#include "Characters/WandererCharacter.h"
#include "Types/CharacterAppearanceTypes.h"
#include "Interfaces/LocomotionAudioReceiver.h"
#include "Interfaces/FootContactHost.h"
#include "Data/Audio/WandererCharacterAudioTypes.h"

#include "WandererPlayableCharacter.generated.h"

class UCharacterCameraZoomComponent;
class UCharacterFillLightingComponent;
class UInputComponent;
class USkeletalMeshComponent;
class UPointLightComponent;
class USpotLightComponent;
class UWandererNaturalInteractionComponent;
class UWandererWaterInteractionComponent;
class UWandererFootstepVFXComponent;
class UFootContactComponent;

// AkAudio
class UAkComponent;
class UWandererCharacterAudioComponent;
struct FWandererCharacterAudioContext;

/** Uses the original Male0 locomotion blend space's walk and run sample speeds. */
UCLASS()
class WANDERER_API AWandererPlayableCharacter : public AWandererCharacter,
                                                public ILocomotionAudioReceiver, public IFootContactHost
{
    GENERATED_BODY()

  public:
    AWandererPlayableCharacter();
    virtual FFootContactSettings GetFootContactSettings() const override;
    virtual FWandererFootContactDebug QueryFootContact(FName InBone) const override;
    virtual int32 GetContactCount() const override;
    virtual int32 GetStrokeCount() const override;


    UFUNCTION(BlueprintCallable, Category = "Movement")
    void StartRunning();

    UFUNCTION(BlueprintCallable, Category = "Movement")
    void StopRunning();

    /** Enable adaptive character and camera fill without changing scene exposure or materials. */
    UFUNCTION(BlueprintCallable, Category = "Appearance|Lighting")
    void SetCharacterFillLightingEnabled(bool bEnabled);

    /** Positive wheel input moves closer; bounded target distance is independent of wall collision.
     */
    UFUNCTION(BlueprintCallable, Category = "Camera|Zoom")
    void ZoomCamera(float WheelDelta);

    UFUNCTION(BlueprintPure, Category = "Camera|Zoom")
    float GetCameraZoomTarget() const;

    virtual const TArray<FWandererAppearancePart>& GetAppearanceParts() const override
    {
        return AppearanceParts;
    }
    virtual void PrepareForMenu() override;
    virtual void OnConstruction(const FTransform& Transform) override;

    // Audio
    void PlayFootstepAudio(const FHitResult* ContactHit = nullptr,
        EWandererAudioPlaybackScope Scope = EWandererAudioPlaybackScope::Default);
    void PlayLandingFootAudio(const FHitResult& Hit, EWandererAudioPlaybackScope Scope);
    virtual void PlayJumpStartAudio() override;
    virtual void OnFootContact(const FHitResult& Hit, bool bLeftFoot, bool bLanding) override;

    UFUNCTION(BlueprintCallable, Category = "Character Audio")
    void NotifyAudioJumpStarted();

    UFUNCTION(BlueprintCallable, Category = "Character Audio")
    void NotifyAudioLanded(const FHitResult& Hit);

    UFUNCTION(BlueprintCallable, Category = "Character Audio")
    void NotifyAudioMovementModeChanged(EMovementMode PreviousMovementMode);

    UFUNCTION(BlueprintCallable, Category = "Character Audio")
    virtual void NotifyAudioSwimStroke() override;

  protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void UnPossessed() override;
    virtual void OnJumped_Implementation() override;
    virtual void Landed(const FHitResult& Hit) override;
    virtual void OnMovementModeChanged(
        EMovementMode PreviousMovementMode, uint8 PreviousCustomMode = 0) override;

  private:
    FWandererCharacterAudioContext MakeCharacterAudioContext(
        const FHitResult* ContactHit = nullptr) const;
    void PlayContactAudio(FName ActionID, const FHitResult* Hit, EWandererAudioPlaybackScope Scope);
    void PlayLocomotionAudio(FName InAction);
    void UpdateSwimmingInput();
    void UpdateSwimmingSurface(float DeltaSeconds);
    void ConfigureCharacterLightingChannels();
    void InitializeCameraZoom();
    void RefreshCameraZoomConfig();

    UPROPERTY(VisibleAnywhere, Category = "Interaction")
    TObjectPtr<UWandererNaturalInteractionComponent> NaturalInteraction;
    UPROPERTY(VisibleAnywhere, Category = "Interaction|Water")
    TObjectPtr<UWandererWaterInteractionComponent> WaterInteraction;
    UPROPERTY(VisibleAnywhere, Category = "Interaction|Footsteps")
    TObjectPtr<UWandererFootstepVFXComponent> FootstepVFX;
    UPROPERTY(VisibleAnywhere, Category = "Interaction|Footsteps")
    TObjectPtr<UFootContactComponent> FootContacts;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "50.0", Units = "cm"))
    float MinimumCameraDistance = 200.0f;
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "50.0", Units = "cm"))
    float MaximumCameraDistance = 800.0f;
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "1.0", Units = "cm"))
    float CameraZoomStep = 40.0f;
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "1.0"))
    float CameraZoomSpeed = 12.0f;

    UPROPERTY(VisibleAnywhere, Category = "Camera|Zoom")
    TObjectPtr<UCharacterCameraZoomComponent> CameraZoom;

    UPROPERTY(VisibleAnywhere, Category = "Appearance|Lighting")
    TObjectPtr<UCharacterFillLightingComponent> FillLighting;

    UPROPERTY(VisibleAnywhere, Category = "Appearance|Lighting")
    TObjectPtr<UPointLightComponent> CharacterKeyLight;

    UPROPERTY(VisibleAnywhere, Category = "Appearance|Lighting")
    TObjectPtr<UPointLightComponent> CharacterFillLight;

    UPROPERTY(VisibleAnywhere, Category = "Appearance|Lighting")
    TObjectPtr<USpotLightComponent> CameraViewLight;

    UPROPERTY(
        EditDefaultsOnly, Category = "Appearance|Lighting", meta = (ClampMin = "0.0", Units = "lm"))
    float MaximumCharacterFillLumens = 48.0f;

    UPROPERTY(
        EditDefaultsOnly, Category = "Appearance|Lighting", meta = (ClampMin = "0.0", Units = "lm"))
    float MaximumCameraFillLumens = 35.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Appearance")
    TArray<FWandererAppearancePart> AppearanceParts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USkeletalMeshComponent>> AppearanceComponents;

    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float WalkingSpeed = 115.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float RunningSpeed = 375.0f;

    // AkAudio
    UPROPERTY(VisibleAnywhere, Category = "Audio|Footsteps")
    TObjectPtr<UAkComponent> FootstepAudio;

    UPROPERTY(VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Audio",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWandererCharacterAudioComponent> CharacterAudio;
    bool bAudioWasSwimming = false;
};
