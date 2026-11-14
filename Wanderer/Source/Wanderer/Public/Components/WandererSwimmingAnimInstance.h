#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

#include "Interfaces/SwimmingPoseProvider.h"
#include "WandererSwimmingAnimInstance.generated.h"

/** Mesh-owned instance; snapshots pawn movement on the game thread for the existing animation
 * graph. */
UCLASS()
class WANDERER_API UWandererSwimmingAnimInstance : public UAnimInstance,
                                                   public ISwimmingPoseProvider
{

    GENERATED_BODY()

  public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    /** Read the evaluated blend intent, rather than treating released input as an immediate pose
     * change. */
    virtual bool IsForwardSwimmingPose() const override;

  private:
    UPROPERTY(
        BlueprintReadOnly, Transient, Category = "Swimming", meta = (AllowPrivateAccess = "true"))
    bool bSwimmingPose = false;

    UPROPERTY(
        BlueprintReadOnly, Transient, Category = "Swimming", meta = (AllowPrivateAccess = "true"))
    float SwimmingPoseSpeed = 0.0f;
};
