#pragma once

#include "CoreMinimal.h"

/** Editor-owned value view. Polling never retains PIE objects or subscribes to gameplay. */
class FGameLogModel
{
  public:
    bool Refresh();
    void Clear();
    void CaptureSpace();
    void SetPreset(int32 InPreset);
    void SetFocus(const FString& InFocus);
    void SetFootstepsEnabled(bool bInEnabled);
    bool IsFootstepsEnabled() const;
    FString GetFocus() const;
    const FString& GetText() const;
    const FString& GetStatus() const;

  private:
    void RefreshHistory();

    uint64 SeenRevision = MAX_uint64;
    uint64 SeenSuppressed = 0;
    uint64 EvictedCount = 0;
    int32 RetainedCount = 0;
    int32 SeenPreset = -1;
    FString Text;
    FString Status;
    TArray<FString> SpatialLines;
};
