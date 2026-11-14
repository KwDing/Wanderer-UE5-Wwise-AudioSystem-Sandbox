#pragma once
#include "UObject/Interface.h"

#include "Types/FootContactTypes.h"

#include "FootContactHost.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UFootContactHost : public UInterface
{
    GENERATED_BODY()
};
/** Owner adapter: configuration and compatibility diagnostics, no component references. */
class WANDERER_API IFootContactHost
{
    GENERATED_BODY()
  public:
    virtual FFootContactSettings GetFootContactSettings() const = 0;
    virtual FWandererFootContactDebug QueryFootContact(FName InBone) const = 0;
    virtual int32 GetContactCount() const = 0;
    virtual int32 GetStrokeCount() const = 0;
};
