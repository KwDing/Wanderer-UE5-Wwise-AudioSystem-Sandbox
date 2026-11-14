#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Types/RadioControlTypes.h"

#include "RadioControlModel.generated.h"

class UTextureRenderTarget2D;

/** Controller-owned stable endpoint; unavailable providers yield a deterministic empty state. */
UCLASS()
class WANDERER_API URadioControlModel : public UObject
{
    GENERATED_BODY()
  public:
    UFUNCTION(BlueprintCallable, Category = "Radio")
    bool Execute(const FRadioControlRequest& InRequest);
    UFUNCTION(BlueprintPure, Category = "Radio")
    FRadioControlSnapshot GetSnapshot() const;

    UTextureRenderTarget2D* GetPreview(EWandererSpeakerItem InItem) const;
    bool RegisterProvider(UObject* InProvider);
    void SetPreviewEnabled(bool bInEnabled);
    void UnregisterProvider(UObject* InProvider);
    void Stop();

  private:
    bool bStopped = false;
    TWeakObjectPtr<UObject> ProviderWeakPtr;
};
