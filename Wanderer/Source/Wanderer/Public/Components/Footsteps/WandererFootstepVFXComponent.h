#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "Types/FootContactTypes.h"

#include "WandererFootstepVFXComponent.generated.h"

class ACharacter;
class UParticleSystem;
class UMaterialInterface;
class USceneComponent;
class UCharacterMovementComponent;

USTRUCT()
struct FWandererTimedFootEffect
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<USceneComponent> Component;
    double ExpiresAt = 0;
};

/**
 * Owns local visual effects; FootContactComponent owns detection and cadence state.
 * The owner routes contact notifications to audio and visuals independently.
 */
UCLASS()
class WANDERER_API UWandererFootstepVFXComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWandererFootstepVFXComponent();

    /**
     * Copies legacy Blueprint overrides without exposing the detector to this component.
     * Call on the game thread through the owner host; refresh the snapshot before sampling
     * so runtime edits retain their previous behavior.
     */
    FFootContactSettings GetContactSettings() const;
    void EmitContact(bool bInLeftFoot);
    void Stop();
    UFUNCTION(BlueprintPure, Category = "Interaction|Footsteps")
    int32 GetFootstepCount() const
    {
        return FootstepCount;
    }
    UFUNCTION(BlueprintPure, Category = "Interaction|Footsteps")
    int32 GetActiveEffectCount() const
    {
        return Effects.Num();
    }
    UFUNCTION(BlueprintPure, Category = "Interaction|Footsteps")
    int32 GetDeferredEffectCount() const
    {
        return DeferredEffectCount;
    }
    UFUNCTION(BlueprintPure, Category = "Interaction|Footsteps")
    int32 GetFootContactCount() const;
    UFUNCTION(BlueprintPure, Category = "Interaction|Footsteps")
    FWandererFootContactDebug GetFootContactDebug(FName FootBone) const;
    UFUNCTION(BlueprintPure, Category = "Character Audio|Swimming")
    int32 GetSwimStrokeCount() const;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

  private:
    friend class FFootEffectLifetimeTest;
    void InitializeAssetReferences();
    void LoadEffects();
    bool PrepareMaterial(UMaterialInterface* Material) const;
    bool PrepareDust(UParticleSystem* Template) const;
    void EmitFootstep(ACharacter& Character);
    void EmitDust(const FHitResult& Hit, int32 SurfaceIndex);
    bool EmitPrint(ACharacter& Character, const FHitResult& Hit, int32 SurfaceIndex);
    void AddEffect(USceneComponent* Component, float Lifetime);
    void PruneEffects();
    void ClearEffects();

    /**
     * These contact and stroke properties remain on the original FootstepVFX subobject
     * because existing Blueprint assets serialize their overrides here. Moving them before
     * migrating those assets would silently restore native defaults. The owner forwards
     * a value snapshot to FootContactComponent; no contact state is executed or stored here.
     * TODO: Migrate asset overrides before removing this compatibility configuration.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction|Footsteps|Contact")
    FName LeftFootBone = TEXT("foot_l");
    UPROPERTY(EditDefaultsOnly, Category = "Interaction|Footsteps|Contact")
    FName RightFootBone = TEXT("foot_r");
    /** Body-relative pose clearance when walking; physical sole clearance when landing. */
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "0.0", Units = "cm"))
    float ContactHeight = 1.0f;
    /** Foot must rise above both this height and ContactHeight before another contact can fire. */
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "0.0", Units = "cm"))
    float ReleaseHeight = 4.0f;
    /** Base sole clearance; grounded adjacent lower treads add their bounded height difference. */
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "0.0", Units = "cm"))
    float GroundSupportTolerance = 12.0f;
    /** Consecutive contact confirmation; no Timer or delayed retry after a closed gate. */
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "0.0", Units = "s"))
    float ContactConfirmationSeconds = 0.015f;
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "1.0", Units = "cm"))
    float GroundTraceHeight = 25.0f;
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Contact",
        meta = (ClampMin = "1.0", Units = "cm"))
    float GroundTraceDepth = 60.0f;
    /** Minimum interval per foot; the opposite foot must not consume this gate. */
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Gate",
        meta = (ClampMin = "0.0", Units = "s"))
    float WalkingGateSeconds = 0.18f;
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Gate",
        meta = (ClampMin = "0.0", Units = "s"))
    float RunningGateSeconds = 0.12f;
    UPROPERTY(EditDefaultsOnly,
        Category = "Interaction|Footsteps|Gate",
        meta = (ClampMin = "0.0", Units = "cm/s"))
    float RunningSpeedThreshold = 245.0f;
    /** Recovery must reach this ratio of capsule half-height before a backward pull can fire. */
    UPROPERTY(EditDefaultsOnly, Category = "Character Audio|Swimming", meta = (ClampMin = "0.0"))
    float SwimStrokeReachRatio = 0.8f;
    /** Treading sculls near the torso instead of reaching out into a breaststroke. */
    UPROPERTY(EditDefaultsOnly, Category = "Character Audio|Swimming", meta = (ClampMin = "0.0"))
    float TreadingReachRatio = 0.1f;
    /** Direction hysteresis rejects stationary noise; fire at pull onset without a position delay.
     */
    UPROPERTY(EditDefaultsOnly,
        Category = "Character Audio|Swimming",
        meta = (ClampMin = "0.0", Units = "cm/s"))
    float MinimumSwimHandSpeed = 5.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Interaction|Footsteps")
    TArray<TSoftObjectPtr<UParticleSystem>> DustAssets;
    UPROPERTY(EditDefaultsOnly, Category = "Interaction|Footsteps")
    TArray<TSoftObjectPtr<UMaterialInterface>> PrintAssets;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UParticleSystem>> DustTemplates;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> PrintMaterials;
    UPROPERTY(Transient)
    TArray<FWandererTimedFootEffect> Effects;

    int32 FootstepCount = 0;
    int32 DeferredEffectCount = 0;
    bool bLeftFoot = true;
    bool bVisualEffectsEnabled = false;
    bool bStopped = false;
    bool bClearingEffects = false;
};
