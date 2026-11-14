#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"
#include "AkSwitchValue.h"
#include "Engine/DataTable.h"
#include "Data/Audio/WandererCharacterAudioProfile.h"

namespace
{
    constexpr int32 MaximumProfileDepth = 8;
}

void UWandererCharacterAudioComponent::ResetProfileCache()
{
    CachedActions.Reset();
    SurfaceSwitches.Reset();
    GaitSwitches.Reset();
    WetnessSwitches.Reset();
    NextAllowedTimes.Reset();
    WarnedActions.Reset();
    LastSkipReasons.Reset();
}

bool UWandererCharacterAudioComponent::BuildProfileCache()
{
    TArray<const UWandererCharacterAudioProfile*> Chain;
    const UWandererCharacterAudioProfile* Current = Profile.Get();

    while (Current)
    {
        if (Chain.Contains(Current) || Chain.Num() >= MaximumProfileDepth)
        {
            return FailConfiguration(
                FString::Printf(TEXT("Profile inheritance contains a cycle or exceeds %d levels."),
                    MaximumProfileDepth));
        }

        Chain.Add(Current);
        Current = Current->ParentProfile.Get();
    }

    if (Chain.IsEmpty())
    {
        return FailConfiguration(TEXT("Character Audio Profile is not assigned."));
    }

    // Root first; children replace matching keys.
    for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
    {
        if (!ReadActionTables(*Chain[Index]) || !ReadSwitchTables(*Chain[Index]))
        {
            return false;
        }
    }

    if (CachedActions.IsEmpty())
    {
        return FailConfiguration(TEXT("Profile has no actions."));
    }

    if (!ValidateAudioActions())
    {
        return false;
    }

    for (const auto& Entry : CachedActions)
    {
        NextAllowedTimes.Add(Entry.Key, FActionCooldowns{});
    }

    return true;
}

bool UWandererCharacterAudioComponent::CacheSwitchRow(const UDataTable& Table,
    FName RowName,
    const FWandererCharacterAudioSwitchRow& Row,
    TSet<FName>& LayerKeys)
{
    const FName SemanticValue = Row.SemanticValue.IsNone() ? RowName : Row.SemanticValue;

    if (Row.Group == EWandererAudioSwitchGroup::Surface)
    {
        const FName Key(*FString::Printf(
            TEXT("Surface:%d"), static_cast<int32>(Row.PhysicalSurface.GetValue())));
        if (Row.PhysicalSurface >= SurfaceType_Max || !IsValid(Row.SwitchValue.Get())
            || LayerKeys.Contains(Key))
        {
            return FailConfiguration(FString::Printf(
                TEXT("%s / %s: invalid or duplicate native Physical Surface mapping (%s)."),
                *Table.GetName(),
                *RowName.ToString(),
                *Key.ToString()));
        }
        LayerKeys.Add(Key);
        SurfaceSwitches.Add(Row.PhysicalSurface, Row.SwitchValue);
        return true;
    }

    TMap<FName, TObjectPtr<UAkSwitchValue>>* Destination = nullptr;

    switch (Row.Group)
    {
    case EWandererAudioSwitchGroup::Gait:
        Destination = &GaitSwitches;
        break;
    case EWandererAudioSwitchGroup::Wetness:
        Destination = &WetnessSwitches;
        break;
    default:
        break;
    }

    const FName Key(
        *FString::Printf(TEXT("%d:%s"), static_cast<int32>(Row.Group), *SemanticValue.ToString()));

    if (!Destination || SemanticValue.IsNone() || !IsValid(Row.SwitchValue.Get())
        || LayerKeys.Contains(Key))
    {
        return FailConfiguration(FString::Printf(
            TEXT("%s / %s: invalid Switch binding; key=%s, duplicate=%d, Switch=%s."),
            *Table.GetName(),
            *RowName.ToString(),
            *SemanticValue.ToString(),
            LayerKeys.Contains(Key),
            *GetNameSafe(Row.SwitchValue.Get())));
    }

    LayerKeys.Add(Key);
    Destination->Add(SemanticValue, Row.SwitchValue);
    return true;
}

bool UWandererCharacterAudioComponent::ReadActionTables(
    const UWandererCharacterAudioProfile& Source)
{
    TSet<FName> LayerKeys;
    for (const TObjectPtr<UDataTable>& TableReference : Source.ActionTables)
    {
        const UDataTable* Table = TableReference.Get();
        if (!IsValid(Table)
            || Table->GetRowStruct() != FWandererCharacterAudioActionRow::StaticStruct())
        {
            return FailConfiguration(FString::Printf(TEXT("Invalid action table %s in Profile %s."),
                *GetNameSafe(Table),
                *Source.GetName()));
        }
        for (const auto& Entry : Table->GetRowMap())
        {
            const auto* Row =
                reinterpret_cast<const FWandererCharacterAudioActionRow*>(Entry.Value);
            if (Entry.Key.IsNone() || LayerKeys.Contains(Entry.Key)
                || !FMath::IsFinite(Row->MinimumInterval) || Row->MinimumInterval < 0.0f)
            {
                return FailConfiguration(FString::Printf(
                    TEXT("%s / %s: invalid name, duplicate row, or invalid interval."),
                    *Table->GetName(),
                    *Entry.Key.ToString()));
            }
            LayerKeys.Add(Entry.Key);
            CachedActions.Add(Entry.Key, *Row);
        }
    }
    return true;
}

bool UWandererCharacterAudioComponent::ReadSwitchTables(
    const UWandererCharacterAudioProfile& Source)
{
    // Match the case-insensitive identity used by the destination FName maps.
    TSet<FName> LayerKeys;

    for (const TObjectPtr<UDataTable>& TableReference : Source.SwitchTables)
    {
        const UDataTable* Table = TableReference.Get();

        if (!IsValid(Table)
            || Table->GetRowStruct() != FWandererCharacterAudioSwitchRow::StaticStruct())
        {
            return FailConfiguration(FString::Printf(TEXT("Invalid switch table %s in Profile %s."),
                *GetNameSafe(Table),
                *Source.GetName()));
        }

        for (const auto& Entry : Table->GetRowMap())
        {
            const auto& Row =
                *reinterpret_cast<const FWandererCharacterAudioSwitchRow*>(Entry.Value);
            if (!CacheSwitchRow(*Table, Entry.Key, Row, LayerKeys))
            {
                return false;
            }
        }
    }

    return true;
}
