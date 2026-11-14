#include "Data/Audio/WandererUIAudioProfile.h"

bool UWandererUIAudioProfile::FindAction(
    FName ActionID, FWandererUIAudioActionRow& OutRow, FString& OutError) const
{
    TSet<const UWandererUIAudioProfile*> Visited;
    const FWandererUIAudioActionRow* Found = nullptr;
    for (const UWandererUIAudioProfile* Profile = this; Profile; Profile = Profile->ParentProfile)
    {
        if (Visited.Contains(Profile) || Visited.Num() >= UIAudioDefaults::MaximumProfileDepth)
        {
            OutError = TEXT("Profile inheritance cycle or more than 8 layers.");
            return false;
        }
        Visited.Add(Profile);
        const FWandererUIAudioActionRow* LayerRow = nullptr;
        for (const UDataTable* Table : Profile->ActionTables)
        {
            if (!IsValid(Table)
                || Table->GetRowStruct() != FWandererUIAudioActionRow::StaticStruct())
            {
                OutError =
                    FString::Printf(TEXT("Invalid UI action table in %s."), *Profile->GetName());
                return false;
            }
            const auto* Row =
                Table->FindRow<FWandererUIAudioActionRow>(ActionID, TEXT("UI Audio"), false);
            if (Row && LayerRow)
            {
                OutError = FString::Printf(
                    TEXT("Duplicate action %s in %s."), *ActionID.ToString(), *Profile->GetName());
                return false;
            }
            if (Row)
            {
                LayerRow = Row;
            }
        }
        if (!Found)
        {
            Found = LayerRow;
        }
    }
    if (!Found)
    {
        OutError = FString::Printf(TEXT("Missing UI action %s."), *ActionID.ToString());
        return false;
    }
    OutRow = *Found;
    return true;
}
