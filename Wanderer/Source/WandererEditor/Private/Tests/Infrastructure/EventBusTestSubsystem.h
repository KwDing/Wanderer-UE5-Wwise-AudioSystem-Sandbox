#pragma once

#include "CoreMinimal.h"
#include "Core/WandererEventBusSubsystem.h"
#include "EventBusTestSubsystem.generated.h"

UCLASS()
class UEventBusTestSubsystem : public UWandererEventBusSubsystem
{
    GENERATED_BODY()
  public:
    TWeakObjectPtr<UWorld> WorldWeakPtr;
    int32 Received = 0;
    bool bStopOnReceive = false;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual UWorld* GetWorld() const override;
    UFUNCTION()
    void OnMessage(FWandererGameEventMessage Message);
};
