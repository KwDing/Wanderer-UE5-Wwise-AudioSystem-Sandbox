#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "InteriorFitSpatialIndex.h"

#include "Interfaces/Audio/AudioGeometryProvider.h"

#include "WandererAudioSplineEmitter.generated.h"

class USplineComponent;
class UWandererAudioSourceComponent;

UENUM(BlueprintType)
enum class EWandererAmbientSplineShape : uint8
{
    Lake,
    Line
};

/** Editable sound footprint. Lake is a horizontal filled polygon; Line is a 3D path. */
UCLASS(BlueprintType)
class WANDERER_API AWandererAudioSplineEmitter : public AActor, public IAudioGeometryProvider
{
    GENERATED_BODY()
  public:
    AWandererAudioSplineEmitter();
    virtual EAudioGeometryKind GetAudioGeometryKind() const override;
    virtual FString GetAudioGeometryError() const override;
    virtual float GetAudioBoundsDistanceM(const FVector& InListener) const override;
    virtual bool QueryAudioGeometry(const FVector& InListener,
                                    float InBlendDistanceM,
                                    FAudioGeometrySample& OutSample) const override;

    UFUNCTION(BlueprintPure, Category = "Ambient Spline")
    FString GetGeometryError() const;
    UFUNCTION(BlueprintPure, Category = "Ambient Spline")
    bool QueryListener(FVector Listener,
                       float BlendDistanceM,
                       FVector& OutPosition,
                       float& OutDistanceM,
                       float& OutInsideBlend) const;
    /** Cheap lower bound used before the segment query. */
    float GetBoundsDistanceM(const FVector& Listener) const;
    UFUNCTION(CallInEditor, Category = "Ambient Spline")
    void RecenterSpline();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ambient Spline")
    TObjectPtr<USplineComponent> Spline;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Audio")
    TObjectPtr<UWandererAudioSourceComponent> AudioSource;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient Spline")
    EWandererAmbientSplineShape Shape = EWandererAmbientSplineShape::Lake;
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "Ambient Spline",
              meta = (ClampMin = "1", Units = "cm"))
    float HeightAboveSurface = 200;
    UPROPERTY(EditAnywhere,
              BlueprintReadWrite,
              Category = "Ambient Spline",
              meta = (ClampMin = "1", Units = "cm"))
    float DepthBelowSurface = 500;

  protected:
    virtual void OnConstruction(const FTransform& Transform) override;

  private:
    bool ContainsLakePoint(const FVector& InListener) const;
    void FindClosestSegment(const FVector& InListener,
                            const FVector& InSearchPoint,
                            const FVector2D& InHeightRange,
                            double& InOutBestDistanceSquared,
                            FVector& OutPosition) const;
    float CalcLakeInsideBlend(const FVector& InListener,
                              const FVector2D& InHeightRange,
                              double InBestDistanceSquared,
                              float InBlendDistanceM) const;
    void UpdateGeometry() const;
    bool SampleSplineSegment(int32 InSegmentIndex) const;
    void UpdateQueryIndex() const;
    void ValidateLake() const;
    bool HasLakeEdgeIntersection(int32 InEdgeIndex,
                                 const FVector2D& InStart,
                                 const FVector2D& InEnd) const;
    mutable uint32 CachedVersion = 0;
    mutable EWandererAmbientSplineShape CachedShape = EWandererAmbientSplineShape::Line;
    mutable bool bCacheReady = false;
    mutable TArray<FVector> Vertices;
    mutable FBox LocalBounds = FBox(ForceInit);
    mutable FString GeometryError;
    mutable FInteriorFitSpatialIndex SegmentIndex;
    mutable TArray<FVector> WorldVertices;
    mutable FTransform CachedQueryTransform;
    mutable bool bQueryIndexReady = false;
};
