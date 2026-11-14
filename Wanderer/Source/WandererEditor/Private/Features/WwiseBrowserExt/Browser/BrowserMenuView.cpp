#include "Features/WwiseBrowserExt/Browser/BrowserMenuView.h"

#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"

#include "IAudiokineticTools.h"
#include "Wwise/WwiseTreeItem.h"

#include "Features/WwiseBrowserExt/Assets/WwiseAssetRepository.h"
#include "Features/WwiseBrowserExt/Browser/BrowserMissingEventDialog.h"
#include "Features/WwiseBrowserExt/Browser/BrowserSyncController.h"
#include "Features/WwiseBrowserExt/BrowserPathUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererWwiseBrowser, Log, All);

FBrowserMenuView::~FBrowserMenuView()
{
    Stop();
}

void FBrowserMenuView::Start()
{
    if (BrowserMenuHandle.IsValid())
    {
        return;
    }
    ++ActivationGeneration;
    Controller =
        MakeShared<FBrowserSyncController>(StaticCastSharedRef<IBrowserInteraction>(AsShared()));
    IAudiokineticTools& Tools =
        FModuleManager::LoadModuleChecked<IAudiokineticTools>(TEXT("AudiokineticTools"));
    BrowserMenuHandle =
        Tools.OnExtendWwiseBrowserContextMenu.AddSP(AsShared(), &FBrowserMenuView::OnExtendBrowser);
    Tools.OnOverrideWwiseBrowserImport.BindSP(
        Controller.ToSharedRef(), &FBrowserSyncController::OnOverrideImport);
    Tools.OnPrepareWwiseBrowserDrag.BindSP(
        Controller.ToSharedRef(), &FBrowserSyncController::OnPrepareDrag);
    FContentBrowserMenuExtender_SelectedPaths Delegate =
        FContentBrowserMenuExtender_SelectedPaths::CreateSP(
            AsShared(), &FBrowserMenuView::OnExtendFolders);
    FolderMenuHandle = Delegate.GetHandle();
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
        .GetAllPathViewContextMenuExtenders()
        .Add(Delegate);
}

void FBrowserMenuView::Stop()
{
    ++ActivationGeneration;
    if (IAudiokineticTools* Tools =
            FModuleManager::GetModulePtr<IAudiokineticTools>(TEXT("AudiokineticTools")))
    {
        Tools->OnExtendWwiseBrowserContextMenu.Remove(BrowserMenuHandle);
        if (Tools->OnOverrideWwiseBrowserImport.IsBoundToObject(Controller.Get()))
        {
            Tools->OnOverrideWwiseBrowserImport.Unbind();
        }
        if (Tools->OnPrepareWwiseBrowserDrag.IsBoundToObject(Controller.Get()))
        {
            Tools->OnPrepareWwiseBrowserDrag.Unbind();
        }
    }
    if (FContentBrowserModule* Content =
            FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
    {
        const FDelegateHandle Handle = FolderMenuHandle;
        Content->GetAllPathViewContextMenuExtenders().RemoveAll(
            [Handle](const FContentBrowserMenuExtender_SelectedPaths& InDelegate)
            {
                return InDelegate.GetHandle() == Handle;
            });
    }
    BrowserMenuHandle.Reset();
    FolderMenuHandle.Reset();
    Controller.Reset();
}

bool FBrowserMenuView::Confirm(const FString& InText)
{
    if (!IsAvailable() || FApp::IsUnattended())
    {
        return false;
    }
    const uint64 Generation = ActivationGeneration;
    const bool bAccepted =
        FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(InText)) == EAppReturnType::Yes;
    return bAccepted && IsAvailable() && ActivationGeneration == Generation;
}

uint64 FBrowserMenuView::GetGeneration() const
{
    return ActivationGeneration;
}

bool FBrowserMenuView::IsAvailable() const
{
    return Controller.IsValid();
}

bool FBrowserMenuView::SelectMissingEvents(TArray<FAssetData>& InOutObjects)
{
    if (!IsAvailable())
    {
        return false;
    }
    const uint64 Generation = ActivationGeneration;
    const bool bAccepted = FBrowserMissingEventDialog::SelectMissingEvents(InOutObjects);
    return bAccepted && IsAvailable() && ActivationGeneration == Generation;
}

void FBrowserMenuView::ShowResult(const FString& InText)
{
    UE_LOG(LogWandererWwiseBrowser, Log, TEXT("Wwise Browser Sync: %s"), *InText);
    if (IsAvailable() && !FApp::IsUnattended())
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(InText));
    }
}

void FBrowserMenuView::OnExtendBrowser(
    FMenuBuilder& InOutMenu, const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    if (!Controller)
    {
        return;
    }
    FBrowserMenuView::BuildMenu(*Controller, InOutMenu, InSelection);
}

TSharedRef<FExtender> FBrowserMenuView::OnExtendFolders(const TArray<FString>& InPaths)
{
    const TSharedRef<FExtender> Extender = MakeShared<FExtender>();
    if (!Controller || !Controller->IsSyncEnabled() || InPaths.IsEmpty()
        || InPaths.ContainsByPredicate(
            [](const FString& InPath)
            {
                return !WwiseBrowserPaths::IsWithin(InPath, FWwiseAssetRepository::Root);
            }))
    {
        return Extender;
    }
    FBrowserMenuView::AddFolderEntry(*Controller, InPaths, Extender);
    return Extender;
}

void FBrowserMenuView::AddFolderEntry(FBrowserSyncController& InOutController,
    const TArray<FString>& InPaths,
    const TSharedRef<FExtender>& InOutExtender)
{
    const TWeakPtr<FBrowserSyncController> ControllerWeakPtr = InOutController.AsShared();
    InOutExtender->AddMenuExtension(TEXT("PathContextBulkOperations"),
        EExtensionHook::After,
        nullptr,
        FMenuExtensionDelegate::CreateLambda(
            [ControllerWeakPtr, InPaths](FMenuBuilder& InOutMenu)
            {
                if (const TSharedPtr<FBrowserSyncController> Owner = ControllerWeakPtr.Pin())
                {
                    InOutMenu.AddMenuEntry(FText::FromString(TEXT("Delete unused Wwise asset…")),
                        FText::FromString(
                            TEXT("删除此目录中生成数据已不存在且没有引用的 Wwise 资产。")),
                        FSlateIcon(),
                        FUIAction(FExecuteAction::CreateSP(Owner.ToSharedRef(),
                            &FBrowserSyncController::OnCleanFolders,
                            InPaths)));
                }
            }));
}

void FBrowserMenuView::AddSyncEntries(FBrowserSyncController& InOutController,
    FMenuBuilder& InOutMenu,
    const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    InOutMenu.AddMenuEntry(FText::FromString(TEXT("复制 / Sync selected to WwiseAudio")),
        FText::FromString(TEXT(
            "按已保存 Wwise Event 路径同步移动和改名，随后提供缺失 Event 删除选项。播放前仍需生成 Banks。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            InOutController.AsShared(), &FBrowserSyncController::OnSyncSelection, InSelection)));
    InOutMenu.AddMenuEntry(FText::FromString(TEXT("删除 Wwise 中已缺失且无引用的 UE Event…")),
        FText::FromString(
            TEXT("按已保存 Events 工程核对所选范围，预览可删项并保留仍被引用的 Event。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(InOutController.AsShared(),
            &FBrowserSyncController::OnDeleteMissingEvents,
            InSelection)));
    if (InSelection.ContainsByPredicate(
            [](const TSharedPtr<FWwiseTreeItem>& InItem)
            {
                return InItem && InItem->IsFolder();
            }))
    {
        InOutMenu.AddMenuEntry(FText::FromString(TEXT("Delete unused asset…")),
            FText::FromString(
                TEXT("预览对应 WwiseAudio 路径下，生成数据中不存在且没有引用的 Wwise 资产。")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(InOutController.AsShared(),
                &FBrowserSyncController::OnCleanSelection,
                InSelection)));
    }
}

void FBrowserMenuView::BuildMenu(FBrowserSyncController& InOutController,
    FMenuBuilder& InOutMenu,
    const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection)
{
    InOutMenu.BeginSection(TEXT("WandererWwiseSync"), FText::FromString(TEXT("WwiseAudio 同步")));
    InOutMenu.AddMenuEntry(FText::FromString(TEXT("Sync to WwiseAudio")),
        FText::FromString(TEXT("开启后，导入和拖放都按完整 Wwise 路径保存到 WwiseAudio。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
                      InOutController.AsShared(), &FBrowserSyncController::OnToggleSync),
            FCanExecuteAction(),
            FIsActionChecked::CreateSP(
                InOutController.AsShared(), &FBrowserSyncController::IsSyncEnabled)),
        NAME_None,
        EUserInterfaceActionType::ToggleButton);
    if (InOutController.IsSyncEnabled())
    {
        AddSyncEntries(InOutController, InOutMenu, InSelection);
    }
    InOutMenu.AddMenuEntry(FText::FromString(TEXT("设为待替换的旧 Wwise 对象")),
        FText::FromString(TEXT("记录此对象关联的 UE 资产，然后右键新 Wwise 对象完成替换。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            InOutController.AsShared(), &FBrowserSyncController::OnRememberSource, InSelection)));
    InOutMenu.AddMenuEntry(FText::FromString(TEXT("用此 Wwise 对象替换旧资产引用…")),
        FText::FromString(
            TEXT("替换已记录的旧对象；未记录时使用 Content Browser 当前选中的旧资产。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(InOutController.AsShared(),
            &FBrowserSyncController::OnReplaceWithSelection,
            InSelection)));
    InOutMenu.EndSection();
}
