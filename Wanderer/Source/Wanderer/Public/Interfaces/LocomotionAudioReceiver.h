#pragma once

#include "UObject/Interface.h"
#include "LocomotionAudioReceiver.generated.h"

struct FHitResult;

/** Game-thread contact notifications. The hit belongs to the current trace. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class ULocomotionAudioReceiver : public UInterface
{
    GENERATED_BODY()
};

class WANDERER_API ILocomotionAudioReceiver
{
    GENERATED_BODY()

  public:
    virtual void OnFootContact(const FHitResult& Hit, bool bLeftFoot, bool bLanding) = 0;
    virtual void PlayJumpStartAudio() = 0;
    virtual void NotifyAudioSwimStroke() = 0;
};
