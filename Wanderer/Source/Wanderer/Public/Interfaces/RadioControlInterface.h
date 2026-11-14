#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Types/RadioControlTypes.h"

#include "RadioControlInterface.generated.h"

class UTextureRenderTarget2D;

UINTERFACE(MinimalAPI)
class URadioControlInterface : public UInterface
{
    GENERATED_BODY()
};

/** Game-thread provider contract; registration is weak and scoped to one controller. */
class WANDERER_API IRadioControlInterface
{
    GENERATED_BODY()
public:
    virtual FRadioControlSnapshot ReadRadioState() const = 0;
    virtual bool ExecuteRadioRequest(const FRadioControlRequest& InRequest) = 0;
    virtual UTextureRenderTarget2D* GetRadioPreview(EWandererSpeakerItem InItem) const = 0;
    virtual void SetRadioPreviewEnabled(bool bInEnabled) = 0;
};
