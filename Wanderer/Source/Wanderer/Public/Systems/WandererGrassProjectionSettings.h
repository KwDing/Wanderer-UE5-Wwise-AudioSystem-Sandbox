#pragma once

#include "CoreMinimal.h"

#include "PCGSettings.h"

#include "WandererGrassProjectionSettings.generated.h"

/** PCG-owned points only: project onto exposed landscape, never onto props or water. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class WANDERER_API UWandererGrassProjectionSettings : public UPCGSettings
{
    GENERATED_BODY()

  public:
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override
    {
        return TEXT("WandererGrassProjection");
    }
    virtual FText GetDefaultNodeTitle() const override
    {
        return NSLOCTEXT("Wanderer", "GrassProjection", "Exposed landscape grass");
    }
    virtual EPCGSettingsType GetType() const override
    {
        return EPCGSettingsType::Spatial;
    }
#endif

    /** Grass mesh's local minimum Z; measured from the selected spawner mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
    double MeshBaseZ = 0;

    /** Minimum clearance around a clump, in cm. */
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = Settings,
        meta = (ClampMin = "0", ClampMax = "100"))
    double ClearanceRadius = 40;

  protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};
