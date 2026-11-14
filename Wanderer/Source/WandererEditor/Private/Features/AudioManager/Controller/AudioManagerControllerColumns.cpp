#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
    constexpr const TCHAR* ColumnSettingsSection = TEXT("Wanderer.WorldAudioManager.Columns");
}

TArray<FAudioManagerController::FColumnDefinition> FAudioManagerController::GetColumnDefinitions(
    int32 InPage)
{
    if (InPage == 1)
    {
        return {{"Name", TEXT("资源组"), 270, true},
            {"Table", TEXT("共享策略 DT"), 170, false},
            {"Row", TEXT("策略行"), 135, false},
            {"Resident", TEXT("常驻"), 80, false},
            {"Load", TEXT("加载 m"), 100, true},
            {"Unload", TEXT("卸载 m"), 100, true},
            {"Delay", TEXT("延迟 s"), 90, false},
            {"Limit", TEXT("区域 Event 预算"), 135, true},
            {"Admitted", TEXT("已准入"), 100, true},
            {"Sources", TEXT("声源数"), 100, false},
            {"Rooms", TEXT("Rooms"), 100, false},
            {"Portals", TEXT("Portals"), 100, false},
            {"Preload", TEXT("预加载 Events"), 135, false},
            {"Dependencies", TEXT("依赖组"), 180, false},
            {"Status", TEXT("状态"), 220, true}};
    }
    return {{"Name", TEXT("对象名称"), 270, true},
        {"Room", TEXT("关联对象（Room / 地表）"), 220, false},
        {"Type", TEXT("类型"), 135, false},
        {"Group", TEXT("资源组"), 110, true},
        {"Event", TEXT("Play Event"), 280, true},
        {"Table", TEXT("配置 DT"), 170, false},
        {"Row", TEXT("配置行"), 135, false},
        {"Area", TEXT("分类标签"), 135, false},
        {"Category", TEXT("声音类别"), 135, false},
        {"Status", TEXT("状态"), 220, true},
        {"Distance", TEXT("距离 m"), 100, false},
        {"Priority", TEXT("优先级"), 100, false},
        {"Playing", TEXT("Playing ID"), 100, false},
        {"Level", TEXT("关卡"), 220, false}};
}

bool FAudioManagerController::IsColumnVisible(int32 InPage, FName InColumn) const
{
    return InColumn == "Name"
           || !(InPage == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns)
                   .Contains(InColumn);
}

void FAudioManagerController::SetColumnVisibility(int32 InPage, FName InColumn, bool bInVisible)
{
    if (bStopped)
    {
        return;
    }
    if (InColumn == "Name"
        || !GetColumnDefinitions(InPage).ContainsByPredicate(
            [InColumn](const auto& Item)
            {
                return Item.Key == InColumn;
            }))
    {
        return;
    }
    auto& Hidden = InPage == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns;
    if (bInVisible)
    {
        Hidden.Remove(InColumn);
    }
    else
    {
        Hidden.Add(InColumn);
    }
    if ((InPage == 1) == (State.ActivePage == 1))
    {
        RebuildColumns();
        RefreshTable();
    }
}

void FAudioManagerController::ResetColumnVisibility(int32 InPage)
{
    if (bStopped)
    {
        return;
    }
    auto& Hidden = InPage == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns;
    Hidden.Reset();
    for (const FColumnDefinition& Column : GetColumnDefinitions(InPage))
    {
        if (!Column.bDefaultVisible && Column.Key != "Name")
        {
            Hidden.Add(Column.Key);
        }
    }
    if ((InPage == 1) == (State.ActivePage == 1))
    {
        RebuildColumns();
        RefreshTable();
    }
}

void FAudioManagerController::ShowAllColumns(int32 InPage)
{
    if (bStopped)
    {
        return;
    }
    (InPage == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns).Reset();
    if ((InPage == 1) == (State.ActivePage == 1))
    {
        RebuildColumns();
        RefreshTable();
    }
}

void FAudioManagerController::LoadColumnVisibility(const FString& InFilename)
{
    if (bStopped)
    {
        return;
    }
    const FString File = InFilename.IsEmpty()
                             ? FPaths::ProjectSavedDir() / TEXT("Config/WorldAudioColumns.ini")
                             : InFilename;
    FConfigFile Settings;
    Settings.Read(File);
    for (int32 Page : {0, 1})
    {
        ResetColumnVisibility(Page);
        if (bStopped)
        {
            return;
        }
        bool bConfigured = false;
        const TCHAR* ConfiguredKey =
            Page == 1 ? TEXT("ResourceColumnsConfigured") : TEXT("SourceColumnsConfigured");
        if (!Settings.GetBool(ColumnSettingsSection, ConfiguredKey, bConfigured) || !bConfigured)
        {
            continue;
        }
        TArray<FString> Saved;
        Settings.GetArray(ColumnSettingsSection,
            Page == 1 ? TEXT("State.HiddenResourceColumns") : TEXT("State.HiddenSourceColumns"),
            Saved);
        auto& Hidden = Page == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns;
        Hidden.Reset();
        for (const FColumnDefinition& Column : GetColumnDefinitions(Page))
        {
            if (Column.Key != "Name" && Saved.Contains(Column.Key.ToString()))
            {
                Hidden.Add(Column.Key);
            }
        }
    }
    RebuildColumns();
}

void FAudioManagerController::SaveColumnVisibility(const FString& InFilename) const
{
    if (bStopped)
    {
        return;
    }
    const FString File = InFilename.IsEmpty()
                             ? FPaths::ProjectSavedDir() / TEXT("Config/WorldAudioColumns.ini")
                             : InFilename;
    FConfigFile Settings;
    Settings.Read(File);
    for (int32 Page : {0, 1})
    {
        TArray<FString> Saved;
        for (FName Column : Page == 1 ? State.HiddenResourceColumns : State.HiddenSourceColumns)
        {
            if (Column != "Name")
            {
                Saved.Add(Column.ToString());
            }
        }
        Saved.Sort();
        Settings.SetBool(ColumnSettingsSection,
            Page == 1 ? TEXT("ResourceColumnsConfigured") : TEXT("SourceColumnsConfigured"),
            true);
        Settings.SetArray(ColumnSettingsSection,
            Page == 1 ? TEXT("State.HiddenResourceColumns") : TEXT("State.HiddenSourceColumns"),
            Saved);
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!Settings.Write(File, false))
    {
        UE_LOG(LogTemp, Warning, TEXT("World Audio: unable to save local column preferences."));
    }
}
