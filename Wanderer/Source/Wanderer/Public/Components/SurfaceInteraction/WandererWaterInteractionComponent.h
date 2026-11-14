#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "Core/WaterContactUtils.h"
#include "Data/SurfaceInteractionData.h"

#include "WandererWaterInteractionComponent.generated.h"

class UStaticMeshComponent;
class ACharacter;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;

/** Value-only sample history, owned by one receiver, never an animation callback. */
struct FWandererWaterContact
{
    FName Bone;
    FVector PreviousPosition = FVector::ZeroVector;
    FVector PreviousRelativePosition = FVector::ZeroVector;
    float StrokeDistance = 0.0f;
    float LastStrikeTime = UE::Wanderer::SurfaceInteraction::NoContactTime;
    float PreviousRelativeVerticalSpeed = 0.0f;
    bool bHasSample = false;
};

/** Fixed-size-budget world-space impulses; they expand from impact, not the moving pawn. */
struct FWandererWaterWave
{
    FVector Position = FVector::ZeroVector;
    float StartTime = 0.0f;
    float Strength = 0.0f;
    float Radius = 0.0f;
};

USTRUCT()
struct FWandererIWSReceiver
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> SolverWeakPtr;
    UPROPERTY(Transient)
    TWeakObjectPtr<UStaticMeshComponent> SurfaceWeakPtr;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> SurfaceTemplate;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
    UPROPERTY(Transient)
    TWeakObjectPtr<UStaticMeshComponent> WaterlineWeakPtr;
    TArray<FWandererWaterBodySection> BodySections;
    FVector LastImpact = FVector::ZeroVector;
    FVector LastFootstep = FVector::ZeroVector;
    TArray<FWandererWaterContact> Contacts;
    TArray<FWandererWaterWave> Waves;
    float LastContactTime = UE::Wanderer::SurfaceInteraction::NoContactTime;
    float PreviousVerticalSpeed = 0.0f;
    float PendingFallSpeed = 0.0f;
    float EntryEnergy = 0.0f;
    float BodyMotion = 0.0f;
    float AnimatedMotion = 0.0f;
    bool bHadBodyContact = false;
    bool bLeftFoot = true;
    bool bWasWet = false;
};

USTRUCT()
struct FWandererIWSSplash
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TWeakObjectPtr<UActorComponent> ComponentWeakPtr;
    UPROPERTY(Transient)
    TWeakObjectPtr<UStaticMeshComponent> ContactSurfaceWeakPtr;
    FName ContactBone;
    float StartTime = 0.0f;
    float Expiry = 0.0f;
};

/**
 * Character-owned visual water contact, independent of surface classification and audio.
 * Borrows world solvers/receivers; owns cleanup of its transient material and effects.
 * Blueprint callbacks can end the owner. Cleanup waits for the active update to unwind.
 */
UCLASS()
class WANDERER_API UWandererWaterInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWandererWaterInteractionComponent();
#if WITH_EDITOR
    void AppendWaterPackagesToCook(TArray<FName>& InOutPackages) const;
#endif
    void ClearWaterRegistration();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetWaterImpactCount() const
    {
        return WaterImpactCount;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetEntrySplashCount() const
    {
        return EntrySplashCount;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetContactSplashCount() const
    {
        return ContactSplashCount;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetRegisteredWaterCount() const
    {
        return RegisteredWaterActors.Num();
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetActiveSplashComponentCount() const
    {
        return Splashes.Num();
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetActiveWaterlineCount() const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Water")
    int32 GetWaterlineSectionCount() const;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;
    virtual void TickComponent(float InDeltaTime,
        ELevelTick InTickType,
        FActorComponentTickFunction* InTickFunction) override;

  private:
    friend struct FSurfaceInteractionTestAccess;

    UPROPERTY(Transient)
    TArray<FWandererIWSReceiver> IWSReceivers;
    UPROPERTY(Transient)
    TArray<FWandererIWSSplash> Splashes;
    UPROPERTY()
    TSoftObjectPtr<UStaticMesh> WaterlineMeshAsset = TSoftObjectPtr<UStaticMesh>(
        FSoftObjectPath(TEXT("/Game/Meshes/Water/SM_ContactWaterGridV2.SM_ContactWaterGridV2")));
    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> WaterlineMesh;
    UPROPERTY()
    TSoftObjectPtr<UMaterialInterface> WaterlineTemplateAsset =
        TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            TEXT("/Game/Materials/Water/M_ContactWaterSurfaceV2.M_ContactWaterSurfaceV2")));
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> WaterlineTemplate;
    UPROPERTY()
    TSoftObjectPtr<UNiagaraSystem> SprayTemplateAsset = TSoftObjectPtr<
        UNiagaraSystem>(FSoftObjectPath(TEXT(
        "/Game/VFX/Niagara/Systems/Water/NS_ContactSpray_Variant02.NS_ContactSpray_Variant02")));
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraSystem> SprayTemplate;
    UPROPERTY()
    TSoftObjectPtr<UNiagaraSystem> SwimmingSprayTemplateAsset =
        TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
            TEXT("/Game/VFX/Niagara/Systems/Water/NS_SwimmingContact.NS_SwimmingContact")));
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraSystem> SwimmingSprayTemplate;

    bool bStopped = true;
    bool bUpdating = false;
    bool bClearing = false;
    bool bPendingClear = false;

    void HideSolverPlane(AActor& InSolver);
    void UpdateReceiverMotion(
        FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter, bool bInIsWet);
    void UpdateReceiverRegistration(
        FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter, bool bInIsWet);
    void WriteBodyParameters(const FWandererIWSReceiver& InReceiver,
        UMaterialInstanceDynamic& InMaterial,
        const UStaticMeshComponent& InSurface);
    void DrawBodyWaterline(FWandererIWSReceiver& InOutReceiver, UStaticMeshComponent& InSurface);
    bool CanUpdate() const;
    void FinishUpdate();
    bool LoadWaterAssets();
    void ReleaseWaterAssets();
    void ResetReceiverContact(FWandererIWSReceiver& InOutReceiver);
    void UpdateLegacyRegistrations(ACharacter& InOutCharacter);
    void FindIWSReceivers();
    void UpdateIWSReceiver(FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter);
    bool BindSurfaceMaterial(
        FWandererIWSReceiver& InOutReceiver, UMaterialInstanceDynamic& InOutSolverMaterial);
    void ReleaseSurfaceMaterial(FWandererIWSReceiver& InOutReceiver);
    void UpdateBodyWaterline(FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter);
    void UpdateWaveParameters(FWandererIWSReceiver& InOutReceiver,
        UMaterialInstanceDynamic& InOutMaterial,
        FBox& InOutBounds);
    void AddWaterWave(FWandererIWSReceiver& InOutReceiver,
        const FVector& InPoint,
        float InStrength,
        float InRadius);
    UStaticMeshComponent* CreateBodyWaterline(AActor& InOutSolver);
    void ReleaseBodyWaterline(FWandererIWSReceiver& InOutReceiver);
    void SuspendWaterReceiver(FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter);
    void EmitEntrySplash(FWandererIWSReceiver& InOutReceiver);
    bool HasSplashDepth(AActor& InOutSolver, const FVector& InLocation) const;
    bool CreateWaterSpray(AActor& InOutSolver,
        const FVector& InLocation,
        float InPower,
        FName InContactBone = NAME_None,
        UStaticMeshComponent* InContactSurface = nullptr);
    bool UpdateSwimmingSpray(FWandererIWSSplash& InOutSplash);
    void UpdateWaterContacts(FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter);
    void EmitWaterContact(FWandererIWSReceiver& InOutReceiver,
        const FVector& InLocation,
        float InSpeed,
        FName InBone);
    void UpdateBoneContact(FWandererIWSReceiver& InOutReceiver,
        FWandererWaterContact& InOutContact,
        ACharacter& InOutCharacter);
    void PruneSplashes(bool bInClearAll);
    bool IsInsideWater(const UStaticMeshComponent& InSurface, const ACharacter& InCharacter) const;
    bool IsOverWater(const UStaticMeshComponent& InSurface, const FVector& InPosition) const;
    int32 WaterImpactCount = 0;
    int32 EntrySplashCount = 0;
    int32 ContactSplashCount = 0;
    TArray<TWeakObjectPtr<AActor>> WaterInteractionActors;
    TSet<TWeakObjectPtr<AActor>> RegisteredWaterActors;
    TArray<FName> BodyStartParameters;
    TArray<FName> BodyEndParameters;
    TArray<FName> BodyRadiusParameters;
    TArray<FName> WavePointParameters;
    TArray<FName> WaveDataParameters;
};
