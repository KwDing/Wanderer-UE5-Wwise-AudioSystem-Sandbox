#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Types/GameStateTypes.h"

#include "WandererGameState.generated.h"

/** Local single-player state. Subscribe here rather than polling widgets. */
UCLASS()
class WANDERER_API AWandererGameState : public AGameStateBase
{
    GENERATED_BODY()

  public:
    UFUNCTION(BlueprintPure, Category = "Wanderer|State")
    EWandererPlayState GetPlayState() const
    {
        return PlayState;
    }

    /** False if rejected, stopped, or cancelled by player teardown during notification. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|State")
    bool SetPlayState(EWandererPlayState NewState);

    UPROPERTY(BlueprintAssignable, Category = "Wanderer|State")
    FWandererPlayStateChanged OnPlayStateChanged;

    /** Requests Normal after an in-flight broadcast; arbitrary nested transitions stay rejected. */
    void RequestPlayerExitReset();

  protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    UPROPERTY(VisibleInstanceOnly, Category = "Wanderer|State")
    EWandererPlayState PlayState = EWandererPlayState::LoadMap;

    bool bStopped = false;
    bool bResetAfterTransition = false;
    bool bOwnsPause = false;
    bool bChangingState = false;

    bool ApplyPauseOwnership(bool bInRequiresPause);
    void CompletePlayerExitReset();
};
