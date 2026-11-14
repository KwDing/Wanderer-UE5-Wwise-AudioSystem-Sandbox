#pragma once

#include "UObject/Interface.h"
#include "SwimmingStateProvider.generated.h"

/** Game-thread query contract; callers do not retain the concrete movement/animation instance. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USwimmingStateProvider : public UInterface
{
    GENERATED_BODY()
};

class WANDERER_API ISwimmingStateProvider
{
    GENERATED_BODY()

  public:
    virtual bool GetSurfaceHeight(float& OutHeight) const = 0;
    virtual bool IsDiving() const = 0;
};
