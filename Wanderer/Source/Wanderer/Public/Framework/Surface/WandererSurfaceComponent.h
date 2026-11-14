#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "WandererSurfaceComponent.generated.h"

class UStaticMeshComponent;
class UDecalComponent;
class ACharacter;
class UMaterialInterface;
class APhysicsVolume;
enum class EWandererGameEvent : uint8;

/** Component-owned value cache; contains no UObject references or external handles. */
struct FWandererCachedPuddleMask
{
    FString MaterialPath;
    int32 Width = 0;
    int32 Height = 0;
    TArray<uint8> Samples;
    float CoreThreshold = 0.55f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWandererSurfaceChanged, FName, PreviousSurface, FName, Surface);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWandererWetnessChanged, FName, PreviousWetness, FName, Wetness);

/** Controller-owned ground listener. A missing/airborne sample preserves the last valid surface. */
/**
 * Controller-owned surface sampling state that survives replacement of its observed Pawn.
 * Scene caches and queries stay on the game thread. Consumers use the owner's surface
 * contract rather than locating this child component; it never plays audio or effects.
 */
UCLASS(ClassGroup = Wanderer, meta = (BlueprintSpawnableComponent))
class WANDERER_API UWandererSurfaceComponent : public UActorComponent
{

    GENERATED_BODY()

  public:
    UWandererSurfaceComponent();

    UPROPERTY(BlueprintAssignable, Category = "Wanderer|Surface")
    FWandererSurfaceChanged OnSurfaceChanged;
    UPROPERTY(BlueprintAssignable, Category = "Wanderer|Surface")
    FWandererWetnessChanged OnWetnessChanged;

    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    FName GetCurrentSurface() const
    {
        return CurrentSurface;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    TEnumAsByte<EPhysicalSurface> GetCurrentPhysicalSurface() const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    FName GetCurrentWetness() const
    {
        return CurrentWetness;
    }

    /** Rebuild visual puddle and shallow pool lookup after streaming or replacing scene actors. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Surface")
    void RefreshSurfaceRegions();

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Surface")
    void SampleSurface();

    /** Classify actual supporting geometry, independently of the cached HUD sample. */
    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    FName ResolveSurface(const FHitResult& Hit) const;

    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    FName ResolveContactWetness(const FHitResult& Hit, const FVector& Feet) const;
    /** Still-water depth above the supporting hit, in cm; independent of animated foot height. */
    UFUNCTION(BlueprintPure, Category = "Wanderer|Surface")
    float GetContactWaterDepth(const FHitResult& Hit) const;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

  private:
    FName ResolvePuddleWetness(const FVector& Ground) const;
    void RefreshPuddleMasks();
    bool IsPuddleCore(
        const UMaterialInterface* Material, const FVector2D& UV, bool bFallback) const;
    float GetPoolWaterDepth(const FVector& Ground) const;
    void PublishContact(FName Surface, FName Wetness);
    void PublishContactEvent(EWandererGameEvent Type, FName PreviousSurface, FName PreviousWetness);
    bool bStopped = false;
    bool bPublishing = false;
    friend class FSurfacePublicationTest;
    friend class USurfaceNotificationProbe;

    /** Shallow standing water is mild; only shoe-covering water becomes wet. */
    UPROPERTY(EditAnywhere,
        Category = "Wanderer|Surface|Wetness",
        meta = (ClampMin = "0.1", Units = "cm"))
    float WetFootstepDepth = 8.0f;

    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Puddles;
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UDecalComponent>> PuddleDecals;
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PoolWaterSurfaces;
    UPROPERTY(Transient)
    TArray<TWeakObjectPtr<APhysicsVolume>> WaterVolumes;

    UPROPERTY(Transient)
    TWeakObjectPtr<ACharacter> ObservedCharacter;

    TArray<FWandererCachedPuddleMask> PuddleMasks;

    FName CurrentSurface;
    FName CurrentWetness = TEXT("dry");
    FName CandidateSurface;
    FName CandidateWetness;
    int32 CandidateSamples = 0;
};
