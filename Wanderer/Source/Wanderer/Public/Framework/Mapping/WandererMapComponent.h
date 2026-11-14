#pragma once

#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "Types/MapTypes.h"

#include "WandererMapComponent.generated.h"

class ACharacter;
class UStaticMeshComponent;

/** Bounded top-down cartography plus an independent connected walkable-cell survey. */
UCLASS(ClassGroup = Wanderer, meta = (BlueprintSpawnableComponent))
class WANDERER_API UWandererMapComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWandererMapComponent();
    const FMapScanSnapshot& GetSnapshot() const
    {
        return Snapshot;
    }
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Map")
    void Rescan();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    bool IsScanning() const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    int32 GetReachableCellCount() const
    {
        return Snapshot.Cells.Num();
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    bool IsTruncated() const
    {
        return Snapshot.bTruncated;
    }

    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    const TArray<FVector>& GetCells() const
    {
        return Snapshot.Cells;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    const TArray<FVector>& GetEntrances() const
    {
        return Snapshot.Entrances;
    }
    const FBox2D& GetMapBounds() const
    {
        return Snapshot.MapBounds;
    }
    float GetCellSize() const
    {
        return CellSize;
    }
    bool HasCell(const FIntPoint& Key) const;
    FIntPoint GetCellKey(const FVector& InPosition) const;
    UFUNCTION(BlueprintPure, Category = "Wanderer|Map")
    const TArray<FWandererMapRegion>& GetRegions() const
    {
        return Snapshot.Regions;
    }

  protected:
    virtual void TickComponent(float InDeltaTime,
        ELevelTick InTickType,
        FActorComponentTickFunction* InThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;

  private:
    UPROPERTY(EditAnywhere, Category = "Wanderer|Map", meta = (ClampMin = "30", ClampMax = "200"))
    float CellSize = 60.0f;
    UPROPERTY(EditAnywhere, Category = "Wanderer|Map", meta = (ClampMin = "1000"))
    float ScanRadius = 20000.0f;
    UPROPERTY(EditAnywhere, Category = "Wanderer|Map", meta = (ClampMin = "100"))
    int32 MaximumCells = 100000;
    UPROPERTY(Transient)
    TWeakObjectPtr<ACharacter> ScanCharacterWeakPtr;

    float Radius = 42.0f;
    float HalfHeight = 96.0f;
    float StepHeight = 45.0f;
    float WalkableFloorZ = 0.71f;
    int32 NextCell = 0;

    bool bNeedsScan = true;

    TArray<TWeakObjectPtr<UStaticMeshComponent>> SurveyWater;
    FCollisionQueryParams SurveyQuery;
    FBox SurveyBounds = FBox(ForceInit);
    float SurveyCellSize = 60.0f;
    FIntPoint SurveyDimensions = FIntPoint::ZeroValue;
    int32 NextSurveyCell = 0;

    FMapScanSnapshot Snapshot;

    bool StartScan(ACharacter& InCharacter);
    bool FindFloor(const FVector& InPosition, FVector& OutFloor) const;
    bool CanTraverse(const FVector& InFrom, const FVector& InTo) const;
    void ExpandCell(int32 Index);
    void AddCell(const FVector& InFloor);
    void FindEntrances();
    void StartSurvey();
    void GatherSurveyBounds(FBox& OutVisibleBounds);
    void GatherSurveyMesh(UStaticMeshComponent& InMesh, FBox& OutVisibleBounds);
    void SampleSurveyCell();
    FName ClassifySurveyHit(const FHitResult& InHit) const;
    bool IsWaterSample(const FVector& InPosition) const;
};
