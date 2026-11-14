#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Types/Audio/AudioSchedulingTypes.h"

#include "AudioBudgetProvider.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAudioBudgetProvider : public UInterface
{
    GENERATED_BODY()
};

/** The entity adapter submits L2 resource data without exposing it through this L1 interface. */
class WANDERER_API IAudioBudgetProvider
{
    GENERATED_BODY()

  public:
    virtual FAudioBudgetLimits GetAudioBudgetLimits() const = 0;
    virtual void SubmitAudioResources(UObject& InReceiver) const = 0;
};
