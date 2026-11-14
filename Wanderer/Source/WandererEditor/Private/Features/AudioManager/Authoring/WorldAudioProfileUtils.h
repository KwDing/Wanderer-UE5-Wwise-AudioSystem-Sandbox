#pragma once

#include "CoreMinimal.h"

class UDataTable;
struct FWandererWorldAudioProfileRow;

namespace WorldAudioProfileUtils
{
    FString SuggestRowName(const FString& InEventName);
    FString ValidateRowName(const FString& InName);
    FString ValidateRow(UDataTable* InTable, FName InName,
        const FWandererWorldAudioProfileRow& InRow);
}
