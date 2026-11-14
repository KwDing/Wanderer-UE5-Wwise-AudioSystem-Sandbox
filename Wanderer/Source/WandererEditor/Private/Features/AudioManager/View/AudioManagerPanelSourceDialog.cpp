#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

void SAudioManagerPanel::ShowSourceDialog(TSharedRef<FSourceDialogDraft> InDraft)
{
    FDetailsViewArgs Args;
    Args.bAllowSearch = true;
    Args.bHideSelectionTip = true;
    FStructureDetailsViewArgs StructArgs;
    TSharedRef<IStructureDetailsView> View =
    FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
        .CreateStructureDetailView(Args, StructArgs, InDraft->Data);
    View->GetOnFinishedChangingPropertiesDelegate()
        .AddLambda(
        [InDraft](const FPropertyChangedEvent& Event)
        {
            if (Event.MemberProperty)
            {
                InDraft->Request.ChangedFields.Add(Event.MemberProperty->GetFName());
            }
            else if (Event.Property)
            {
                InDraft->Request.ChangedFields.Add(Event.Property->GetFName());
            }
        });
    const bool bImport = InDraft->Request.Action == EAudioSourceAction::ImportGeometry;
    const bool bCreate = InDraft->Request.Action != EAudioSourceAction::Edit;
    const FString Title = bImport   ? TEXT("从 IF 几何创建声源")
    : bCreate ? TEXT("新建选中位置声源")
    : TEXT("修改选中位置声源");
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(Title))
        .ClientSize(FVector2D(720, 720))
        .SupportsMinimize(false)
        .SupportsMaximize(false);
    Window->SetContent(BuildSourceDialogContents(InDraft, View->GetWidget()
        .ToSharedRef(), Window));
    FSlateApplication::Get()
        .AddModalWindow( Window, FSlateApplication::Get()
        .FindWidgetWindow(AsShared()));
    return;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildSourceProfileName(TSharedRef<FSourceDialogDraft> InDraft)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(TEXT("配置表行名：") + InDraft->Request.TablePath.ToString()))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SEditableTextBox)
            .HintText(FText::FromString(TEXT("选择 Play Event 后建议行名，可自行修改")))
            .ToolTipText(FText::FromString(
                TEXT("批量修改保留未编辑字段；最终配置不同时按行名追加 _2、_3 等后缀。")))
            .Text_Lambda([InDraft]
            {
                return FText::FromString(FAudioManagerController::GetSourceProfileName(*InDraft));
            })
            .OnTextChanged_Lambda([InDraft](const FText& InText)
            {
                InDraft->bCustomProfileRowName = true;
                InDraft->ProfileRowName = InText.ToString();
            })
        ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildSourceDialogActions(
    TSharedRef<FSourceDialogDraft> InDraft, TSharedRef<SWindow> InWindow)
{
    const bool bCreate = InDraft->Request.Action != EAudioSourceAction::Edit;
    const TWeakPtr<SWindow> WindowWeakPtr = InWindow;
    const TWeakPtr<FAudioManagerController> ManagerWeakPtr = Controller;
    return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
        [
            SNew(SButton)
                .Text(FText::FromString(bCreate ? TEXT("确认创建") : TEXT("应用修改")))
                .OnClicked_Lambda(
                [InDraft, ManagerWeakPtr, WindowWeakPtr]
                {
                    const TSharedPtr<FAudioManagerController> Manager = ManagerWeakPtr.Pin();
                    if (Manager && Manager->ApplySourceDraft(InDraft))
                    {
                        if (const TSharedPtr<SWindow> Dialog = WindowWeakPtr.Pin())
                        {
                            Dialog->RequestDestroyWindow();
                        }
                    }
                    return FReply::Handled();
                })
        ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8, 0)
        [
            SNew(SButton)
                .Text(FText::FromString(TEXT("取消")))
                .OnClicked_Lambda(
                [WindowWeakPtr]
                {
                    if (TSharedPtr<SWindow> Dialog = WindowWeakPtr.Pin())
                    {
                        Dialog->RequestDestroyWindow();
                    }
                    return FReply::Handled();
                })
        ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildSourceDialogContents(
    TSharedRef<FSourceDialogDraft> InDraft,
    TSharedRef<SWidget> InDetails,
    TSharedRef<SWindow> InWindow)
{
    const bool bImport = InDraft->Request.Action == EAudioSourceAction::ImportGeometry;
    const bool bCreate = InDraft->Request.Action != EAudioSourceAction::Edit;
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding( 8)
    [
        SNew(STextBlock)
            .AutoWrapText(true)
            .Text(GetSourceExplanation(InDraft))
    ]
        + SVerticalBox::Slot()
        .FillHeight(1)
        .Padding(8)
    [
        InDetails
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
    [
        BuildSourceProfileName(InDraft)
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
    [
        SNew(SEditableTextBox)
            .Visibility(bImport ? EVisibility::Visible : EVisibility::Collapsed)
            .HintText(FText::FromString(TEXT("Resource Group（留空为独立调度）")))
            .OnTextChanged_Lambda(
            [InDraft](const FText& Text)
            {
                InDraft->Request.ResourceGroup = FName(*Text.ToString()
                .TrimStartAndEnd());
            })
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
    [
        SNew(SCheckBox)
            .Visibility(bCreate ? EVisibility::Collapsed : EVisibility::Visible)
            .OnCheckStateChanged_Lambda(
            [InDraft](ECheckBoxState State)
            {
                InDraft->Request.bApplyAll = State == ECheckBoxState::Checked;
            })
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("将全部配置统一应用到所选声源（覆盖未编辑的差异值）")))
        ]
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8)
    [
        SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda(
            [InDraft]
            {
                return InDraft->Result;
            })
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding( 8)
    [
        BuildSourceDialogActions(InDraft, InWindow)
    ];
}

FText SAudioManagerPanel::GetSourceExplanation(TSharedRef<FSourceDialogDraft> InDraft) const
{
    const bool bImport = InDraft->Request.Action == EAudioSourceAction::ImportGeometry;
    const bool bCreate = InDraft->Request.Action != EAudioSourceAction::Edit;
    return FText::FromString( bImport
        ? TEXT( "复制 Outliner 所选 IF 几何并创建独立声源；Mode 按 Area/Spline 自动设置，保留完整昼夜配置。闭合样条"
        "为 Lake，开放样条为 Line。沿用现有几何同步；整批可撤销。确认后保存地图和配置表。")
        : FString::Printf(TEXT( "%s %d 个目标。Mode：Point 点 / Room 房间 / Global 全局 / Area 面 / Spline 样条"
        "。Night 为空沿用 Play。\n%s\nArea/Spline 新建为关闭播放的初始形状，请编辑边界后启用。保留 Event"
        " 原有加载设置；确认后需保存地图和配置表。"),
        bCreate ? TEXT("新建") : TEXT("修改已有声源"),
        bCreate ? InDraft->Request.Targets.Num() : InDraft->Request.Sources.Num(),
        bCreate
        ? TEXT("Room 模式直接挂组件，不新建 Actor；已有底噪会跳过。")
        : TEXT( "下方以第一个声源为初始值（其它对象可能不同）；默认只应用本次编辑过的字段。")));
}

