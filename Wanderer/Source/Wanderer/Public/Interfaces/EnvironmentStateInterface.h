#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "EnvironmentStateInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEnvironmentStateInterface : public UInterface
{
    GENERATED_BODY()
};

/** Read-only world state; consumers do not depend on the implementing sibling system. */
class WANDERER_API IEnvironmentStateInterface
{
    GENERATED_BODY()

  public:
    virtual bool IsNight() const = 0;
};
