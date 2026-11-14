#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/GameEventTypes.h"
#include "WandererEventBusSubsystem.generated.h"

/** Game-instance subscription owner. Synchronous, game-thread delivery; no retained message
 * history. */
UCLASS()
class WANDERER_API UWandererEventBusSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
  public:
    UPROPERTY(BlueprintAssignable, Category = "Wanderer|Events")
    FWandererGameEventReceived OnGameEvent;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(
        BlueprintPure, Category = "Wanderer|Events", meta = (WorldContext = "WorldContextObject"))
    static UWandererEventBusSubsystem* GetEventBus(const UObject* WorldContextObject);

    /** Reentrant delivery is depth-limited. Shutdown rejects new publishes and later phases,
     * but cannot cancel handlers already copied into an in-progress delegate invocation list.
     */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Events")
    void Publish(const FWandererGameEventMessage& Message);

    /** Process-wide observers own their handles. Only copy values synchronously; never retain
     * World/message references or mutate gameplay. One GI shutting down must not clear this bus.
     */
    static FWandererGameEventObserved& OnEventObserved();

  private:
    bool CanPublish(const UWorld* InWorld) const;
    bool bStopped = false;
    int32 DispatchDepth = 0;
};
