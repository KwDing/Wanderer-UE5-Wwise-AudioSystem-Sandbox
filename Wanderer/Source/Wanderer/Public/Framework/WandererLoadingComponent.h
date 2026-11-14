#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WandererLoadingComponent.generated.h"

/** Game-thread scene-entry gate; failure deliberately leaves the loading overlay open. */
UCLASS()
class WANDERER_API UWandererLoadingComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UWandererLoadingComponent();
    UFUNCTION(BlueprintPure, Category = "Wanderer|Loading")
    bool IsLoading() const
    {
        return bLoading;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Loading")
    float GetProgress() const
    {
        return Progress;
    }
    UFUNCTION(BlueprintPure, Category = "Wanderer|Loading")
    FText GetStatus() const
    {
        return Status;
    }
    void StartLoading();
    void Stop();

  protected:
    virtual void TickComponent(float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    float Progress = 0;
    FText Status;
    double StartedAt = 0;
    double NextStreamingCheckAt = 0;
    bool bStreamingReady = false;
    bool bStartedMapScan = false;
    int32 ReadyFrames = 0;
    bool bLoading = true;
    bool bOwnsInputLock = false;
    bool bFailed = false;
    bool bStopped = false;

    bool IsSceneReady();
    bool AreWorldResourcesReady();
    bool UpdateStreamingReadiness();
    bool UpdateMapReadiness();
    void CompleteEntry();
    bool FinishVisibleResourceStreaming();
    void ReleaseInput();
};
