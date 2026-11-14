#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Interfaces/SwimmingStateProvider.h"
#include "WandererSwimmingMovementComponent.generated.h"

/** Surface cap and camera-directed diving; collision remains engine-owned. */
UCLASS()
class WANDERER_API UWandererSwimmingMovementComponent : public UCharacterMovementComponent,
                                                        public ISwimmingStateProvider
{

    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintPure, Category = "Swimming")
    virtual bool GetSurfaceHeight(float& OutHeight) const override;
    UFUNCTION(BlueprintPure, Category = "Swimming")
    virtual bool IsDiving() const override;

  protected:
    virtual void PhysSwimming(float DeltaSeconds, int32 Iterations) override;
    virtual void OnMovementModeChanged(
        EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

  private:
    const APhysicsVolume* FindContainingWaterVolume() const;
    float GetSurfaceTargetHeight(float SurfaceHeight) const;
    void KeepHeadAboveSurface(float SurfaceHeight, float DeltaSeconds);
    void FinishSurfaceAscent(float SurfaceHeight);

    bool bDiving = false;

    UPROPERTY(EditDefaultsOnly, Category = "Swimming", meta = (ClampMin = "0", Units = "cm"))
    float HeadClearance = 26.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Swimming", meta = (ClampMin = "0", Units = "cm"))
    float ActiveSwimmingHeadClearance = 12.0f;
};
