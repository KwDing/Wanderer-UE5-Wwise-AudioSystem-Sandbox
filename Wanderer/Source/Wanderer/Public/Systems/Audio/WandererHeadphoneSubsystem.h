#pragma once

#include "Subsystems/GameInstanceSubsystem.h"

#include "WandererHeadphoneSubsystem.generated.h"

/**
 * GameInstance facade for the process-wide Wwise headphone mix, not OS device selection.
 * The most recent successful writer owns exit reset; another instance cannot undo it.
 * All access is game-thread only. Requests before Wwise readiness fail without being queued.
 */
UCLASS()
class WANDERER_API UWandererHeadphoneSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

  public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Reads the global applied value when available, otherwise the last acknowledged value. */
    bool IsUsingHeadphone() const;
    /** Returns false on shutdown or SDK failure; no UI flag is optimistically committed. */
    bool TrySetUsingHeadphone(bool bInEnabled);
    /** Compatibility entry point for existing native callers. */
    void SetUsingHeadphone(bool bEnabled);

  private:
    bool bUsingHeadphone = false;
    bool bStopped = false;
};
