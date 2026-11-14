#pragma once

#include "UObject/Interface.h"
#include "SwimmingPoseProvider.generated.h"

/** Game-thread query contract; callers do not retain the concrete movement/animation instance. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USwimmingPoseProvider : public UInterface
{
    GENERATED_BODY()
};

class WANDERER_API ISwimmingPoseProvider
{
    GENERATED_BODY()

  public:
    virtual bool IsForwardSwimmingPose() const = 0;
};
