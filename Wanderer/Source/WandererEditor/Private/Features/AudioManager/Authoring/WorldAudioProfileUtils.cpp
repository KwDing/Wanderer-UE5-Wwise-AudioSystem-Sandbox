#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"

#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"

#include "Data/Audio/WandererWorldAudioTypes.h"

FString WorldAudioProfileUtils::SuggestRowName(const FString& InEventName)
{
    FString Name = InEventName;
    Name.RemoveFromStart(TEXT("Play_"), ESearchCase::IgnoreCase);
    return Name.ToUpper();
}

FString WorldAudioProfileUtils::ValidateRowName(const FString& InName)
{
    if (InName.IsEmpty() || InName.Len() >= NAME_SIZE || FName(*InName).IsNone())
    {
        return TEXT("请输入有效的配置行名（不能为 None 或超长名称）。");
    }
    FText Reason;
    if (!FName::IsValidXName(InName, FString(INVALID_NAME_CHARACTERS) + TEXT("/\\"), &Reason)
        || InName != InName.TrimStartAndEnd())
    {
        return TEXT("行名不能包含非法字符或首尾空格。");
    }
    return FString();
}

FString WorldAudioProfileUtils::ValidateRow(UDataTable* InTable, FName InName,
    const FWandererWorldAudioProfileRow& InRow)
{
    if (!InTable || InTable->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return TEXT("请选择有效的 World Audio 配置表。");
    }
    const FWandererWorldAudioProfileRow* Existing =
        InTable->FindRow<FWandererWorldAudioProfileRow>(InName, TEXT("Profile naming"), false);
    if (Existing
        && !FWandererWorldAudioProfileRow::StaticStruct()->CompareScriptStruct(Existing, &InRow, 0))
    {
        return TEXT("该行名已存在且配置不同；请输入新名称，不会覆盖已有行。");
    }
    return FString();
}
