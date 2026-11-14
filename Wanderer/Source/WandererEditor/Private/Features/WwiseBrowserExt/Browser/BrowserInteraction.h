#pragma once

#include "CoreMinimal.h"

struct FAssetData;

/** Presentation contract; the workflow never depends on a concrete Slate view. */
class IBrowserInteraction
{
public:
    virtual ~IBrowserInteraction() = default;
    virtual bool Confirm(const FString& InText) = 0;
    virtual uint64 GetGeneration() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual bool SelectMissingEvents(TArray<FAssetData>& InOutObjects) = 0;
    virtual void ShowResult(const FString& InText) = 0;
};
