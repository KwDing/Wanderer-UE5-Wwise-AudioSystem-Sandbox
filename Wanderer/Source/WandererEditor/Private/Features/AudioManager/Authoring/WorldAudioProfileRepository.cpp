#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "AkAudioEvent.h"

#include "Data/Audio/WandererWorldAudioTypes.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"

bool FWorldAudioProfileRepository::Matches(const UDataTable& InTable, FName InName,
    const FWandererWorldAudioProfileRow& InRow)
{
    if (InTable.GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return false;
    }
    const FWandererWorldAudioProfileRow* Existing =
        InTable.FindRow<FWandererWorldAudioProfileRow>(InName, TEXT("Audio authoring"), false);
    return Existing
        && FWandererWorldAudioProfileRow::StaticStruct()->CompareScriptStruct(Existing, &InRow, 0);
}

FDataTableRowHandle FWorldAudioProfileRepository::FindOrAdd(UDataTable* InTable,
    const FWandererWorldAudioProfileRow& InRow, UAkAudioEvent* InEvent,
    FName InName, FName& OutCreatedName)
{
    OutCreatedName = NAME_None;
    FDataTableRowHandle Handle;
    if (!IsInGameThread() || !IsValid(InTable) || !IsValid(InEvent)
        || InTable->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return Handle;
    }
    const TStrongObjectPtr<UDataTable> TablePin(InTable);
    const FName Name = InName.IsNone() ? FindAutomaticName(*InTable, InRow, *InEvent) : InName;
    if (!WorldAudioProfileUtils::ValidateRowName(Name.ToString()).IsEmpty()
        || !WorldAudioProfileUtils::ValidateRow(InTable, Name, InRow).IsEmpty())
    {
        return Handle;
    }
    if (!InTable->GetRowMap().Contains(Name))
    {
        CreateRow(*InTable, Name, InRow, OutCreatedName);
    }
    if (IsValid(InTable) && Matches(*InTable, Name, InRow))
    {
        Handle.DataTable = InTable;
        Handle.RowName = Name;
    }
    return Handle;
}

bool FWorldAudioProfileRepository::RemoveCreated(UDataTable& InTable, FName InName,
    const FWandererWorldAudioProfileRow& InRow)
{
    if (!InTable.GetRowMap().Contains(InName))
    {
        return true;
    }
    if (!Matches(InTable, InName, InRow))
    {
        return false;
    }
    InTable.RemoveRow(InName);
    return !InTable.GetRowMap().Contains(InName);
}

void FWorldAudioProfileRepository::CreateRow(UDataTable& InTable, FName InName,
    const FWandererWorldAudioProfileRow& InRow, FName& OutCreatedName)
{
    InTable.SetFlags(RF_Transactional);
    InTable.Modify();
    if (!IsValid(&InTable) || InTable.GetRowMap().Contains(InName))
    {
        return;
    }
    OutCreatedName = InName;
    InTable.AddRow(InName, InRow);
    if (IsValid(&InTable))
    {
        InTable.MarkPackageDirty();
    }
}

FName FWorldAudioProfileRepository::FindAutomaticName(const UDataTable& InTable,
    const FWandererWorldAudioProfileRow& InRow, const UAkAudioEvent& InEvent)
{
    TArray<FName> Names = InTable.GetRowNames();
    Names.Sort(FNameLexicalLess());
    for (FName Name : Names)
    {
        if (Matches(InTable, Name, InRow))
        {
            return Name;
        }
    }
    const FString Base = WorldAudioProfileUtils::SuggestRowName(InEvent.GetName());
    FName Name(*Base);
    constexpr int32 FirstDuplicateSuffix = 2;
    int32 Suffix = FirstDuplicateSuffix;
    while (Names.Contains(Name))
    {
        Name = FName(*FString::Printf(TEXT("%s_%d"), *Base, Suffix++));
    }
    return Name;
}
