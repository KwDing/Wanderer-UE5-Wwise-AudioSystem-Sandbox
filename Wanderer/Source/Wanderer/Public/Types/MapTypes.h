#pragma once

#include "CoreMinimal.h"

#include "MapTypes.generated.h"

/** A horizontal run of equally classified top-down map samples, owned as values. */
USTRUCT(BlueprintType)
struct FWandererMapRegion
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    FVector2D Center = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FVector2D Size = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FName Surface;
};

USTRUCT(BlueprintType)
struct FWandererMapDoorMarker
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FName ID;
    /** Full map package path; PIE prefixes are removed when matching. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FString ScenePackage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FString Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FVector DoorLocation = FVector::ZeroVector;
    /** Supported ground outside the doorway; never the inside of the building. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FVector DestinationFloor = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
    FRotator ArrivalRotation = FRotator::ZeroRotator;
};

/** Controller-owned values. Borrow only during a game-thread call; rescans invalidate references.
 */
struct FMapScanSnapshot
{
    TArray<FVector> Cells;
    TMap<FIntPoint, int32> CellLookup;
    TArray<FVector> Entrances;
    TArray<FWandererMapRegion> Regions;
    FBox2D MapBounds = FBox2D(ForceInit);
    FVector Seed = FVector::ZeroVector;
    float CellSize = 60.0f;
    bool bScanning = false;
    bool bSurveying = false;
    bool bTruncated = false;
};

struct FMapPlayerSnapshot
{
    FVector Location = FVector::ZeroVector;
    FVector Forward = FVector::ForwardVector;
    bool bVisible = false;
};
