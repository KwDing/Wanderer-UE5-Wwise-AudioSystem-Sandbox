#include "Features/AudioManager/Controller/AudioManagerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SHeaderRow.h"

namespace
{
bool CaptureAudioWindow(const TSharedRef<SWindow>& Window, const FString& Filename)
{
    Window->SlatePrepass();
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!FSlateApplication::Get().TakeScreenshot(Window, Pixels, Size)) return false;
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
    return FFileHelper::SaveArrayToFile(PNG, *Filename);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioColumnSettingsTest, "Wanderer.Audio.Manager.ColumnSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioColumnSettingsTest::RunTest(const FString& Parameters)
{
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->SpawnActor<AWandererWorldAudioBudget>();
    auto* Common = NewObject<UAkAudioEvent>(GetTransientPackage(), TEXT("Preview_Building"));
    auto* Special = NewObject<UAkAudioEvent>(GetTransientPackage(), TEXT("Preview_RoomSpecial"));
    auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(World, AAkSpatialAudioVolume::StaticClass(), FVector(200), FTransform::Identity));
    if (!Room) return false;
    Room->SetActorLabel(TEXT("ACOUSTIC_ROOM__Special")); Room->Room->AutoPost = false; Room->Room->bEnable = true;
    auto* Building = UAudioAuthoringLibrary::CreateBuildingAmbience({Room}, TEXT("PreviewBuilding"), Common);
    if (!Building) return false;
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row; Row.Mode = EWandererWorldAudioMode::Room; Row.PlayEvent = Special;
    Table->AddRow(TEXT("Special"), Row);
    auto* Source = UAudioAuthoringLibrary::AttachRoomAudioComponent(Room, nullptr);
    if (!TestNotNull(TEXT("Preview Room layer component"), Source)) return false;
    Source->Profile.DataTable = Table; Source->Profile.RowName = TEXT("Special");
    TSharedPtr<SAudioManagerPanel> Manager = SNew(SAudioManagerPanel);
    Manager->Controller->ResetColumnVisibility(0);
    TestEqual(TEXT("Default source layout is compact"), Manager->Header->GetColumns().Num(), 4);
    Manager->Controller->SetColumnVisibility(0, "Name", false);
    TestTrue(TEXT("Object name cannot be hidden"), Manager->Controller->IsColumnVisible(0, "Name"));
    Manager->Controller->SetColumnVisibility(0, "Type", true);
    bool bHasTypeColumn = false;
    for (const auto& Column : Manager->Header->GetColumns()) bHasTypeColumn |= Column.ColumnId == "Type";
    TestTrue(TEXT("Optional column added to real header"), bHasTypeColumn);
    Manager->Controller->State.SortColumn = "Event"; Manager->Controller->SetColumnVisibility(0, "Event", false);
    TestEqual(TEXT("Hidden sort resets to visible Name"), Manager->Controller->State.SortColumn, FName(TEXT("Name")));
    Manager->Controller->SetColumnVisibility(1, "Load", false);
    TestTrue(TEXT("Resource layout does not hide source Group"), Manager->Controller->IsColumnVisible(0, "Group"));
    const FString ReportDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../AgentKit/reports/audio_manager_refinement/"));
    IFileManager::Get().MakeDirectory(*ReportDir, true);
    const FString SettingsFile = ReportDir / TEXT("column_preferences.ini");
    Manager->Controller->SaveColumnVisibility(SettingsFile);
    Manager->Controller->ShowAllColumns(0); Manager->Controller->ShowAllColumns(1); Manager->Controller->LoadColumnVisibility(SettingsFile);
    TestFalse(TEXT("Source preference roundtrip"), Manager->Controller->IsColumnVisible(0, "Event"));
    TestFalse(TEXT("Resource preference roundtrip"), Manager->Controller->IsColumnVisible(1, "Load"));
    Manager->Controller->ShowAllColumns(0); Manager->Controller->SaveColumnVisibility(SettingsFile);
    Manager->Controller->ResetColumnVisibility(0); Manager->Controller->LoadColumnVisibility(SettingsFile);
    TestEqual(TEXT("All-visible preference roundtrip handles empty hidden list"), Manager->Header->GetColumns().Num(), Manager->Controller->GetColumnDefinitions(0).Num());
    Manager->Controller->ResetColumnVisibility(0);
    const bool bVisual = FParse::Param(FCommandLine::Get(), TEXT("AudioManagerVisualProbe"));
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("World Audio · 合并 Event 与常用列"))).ClientSize(FVector2D(1450, 650))[Manager.ToSharedRef()];
    FSlateApplication::Get().AddWindow(Window, bVisual);
    if (bVisual) TestTrue(TEXT("Capture compact merged Event table"), CaptureAudioWindow(Window, ReportDir / TEXT("compact.png")));
    auto Popup = Manager->BuildColumnSettings();
    auto PopupWindow = SNew(SWindow).Title(FText::FromString(TEXT("显示列设置"))).ClientSize(FVector2D(300, 500))[Popup];
    FSlateApplication::Get().AddWindow(PopupWindow, bVisual);
    if (bVisual) TestTrue(TEXT("Capture mandatory-name column chooser"), CaptureAudioWindow(PopupWindow, ReportDir / TEXT("columns.png")));
    const TWeakPtr<SAudioManagerPanel> WeakManager = Manager;
    Window->SetContent(SNullWidget::NullWidget); Manager.Reset();
    TestFalse(TEXT("Detached popup does not retain manager"), WeakManager.IsValid());
    Popup->SlatePrepass();
    Window->RequestDestroyWindow(); PopupWindow->RequestDestroyWindow();
    FAutomationEditorCommonUtils::CreateNewMap();
    return true;
}
#endif
