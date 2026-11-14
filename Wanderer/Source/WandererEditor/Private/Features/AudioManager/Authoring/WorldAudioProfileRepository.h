#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

class UAkAudioEvent;
struct FWandererWorldAudioProfileRow;

/** Game-thread table writes; callers own the encompassing transaction and rollback journal. */
class FWorldAudioProfileRepository
{
public:
    static bool Matches(const UDataTable& InTable, FName InName,
        const FWandererWorldAudioProfileRow& InRow);
    /** OutCreatedName is set before AddRow broadcasts, including when a callback invalidates it. */
    static FDataTableRowHandle FindOrAdd(UDataTable* InTable,
        const FWandererWorldAudioProfileRow& InRow, UAkAudioEvent* InEvent,
        FName InName, FName& OutCreatedName);
    /** Does not erase a row subsequently replaced by an unrelated callback. */
    static bool RemoveCreated(UDataTable& InTable, FName InName,
        const FWandererWorldAudioProfileRow& InRow);

private:
    static void CreateRow(UDataTable& InTable, FName InName,
        const FWandererWorldAudioProfileRow& InRow, FName& OutCreatedName);
    static FName FindAutomaticName(const UDataTable& InTable,
        const FWandererWorldAudioProfileRow& InRow, const UAkAudioEvent& InEvent);
};
