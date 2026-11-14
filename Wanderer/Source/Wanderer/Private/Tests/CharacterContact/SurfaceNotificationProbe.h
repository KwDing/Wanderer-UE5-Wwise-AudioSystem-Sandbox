#pragma once
#include "UObject/Object.h"

#include "SurfaceNotificationProbe.generated.h"
class UWandererSurfaceComponent;
UCLASS()
class USurfaceNotificationProbe : public UObject
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    TObjectPtr<UWandererSurfaceComponent> Surface;
    int32 SurfaceCalls = 0;
    int32 WetnessCalls = 0;
    bool bDestroyOnSurface = false;
    FName ObservedWetness;
    UFUNCTION()
    void OnSurface(FName InPrevious, FName InCurrent);
    UFUNCTION()
    void OnWetness(FName InPrevious, FName InCurrent);
};
