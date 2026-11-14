#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "IDetailsView.h"
#include "InputCoreTypes.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

namespace
{
    constexpr float ToolbarActionSize = 72.0f;

    TSharedRef<SWidget> BuildSquareToolbarAction(const FText& InLabel,
        const FText& InTooltip,
        const FOnClicked& InOnClicked,
        TAttribute<bool> InEnabled = true)
    {
        return SNew(SBox)
            .WidthOverride(ToolbarActionSize)
            .HeightOverride(ToolbarActionSize)
        [
            SNew(SButton)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .ToolTipText(InTooltip)
                .IsEnabled(InEnabled)
                .OnClicked(InOnClicked)
            [
                SNew(STextBlock)
                    .Text(InLabel)
                    .Justification(ETextJustify::Center)
            ]
        ];
    }
} // namespace

TSharedRef<SWidget> SAudioManagerPanel::BuildPageTabs()
{
    TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
    const TCHAR* Names[] = {TEXT("音频对象"), TEXT("区域预算"), TEXT("建筑底噪")};
    const int32 Pages[] = {0, 1, 3};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const int32 Page = Pages[Index];
        Tabs->AddSlot()
            .AutoWidth()
            .Padding(2)
        [
            SNew(SButton)
                .Text(FText::FromString(Names[Index]))
                .ButtonColorAndOpacity_Lambda(
                [this, Page]
                {
                    return Controller->GetState().ActivePage == Page
                    ? FLinearColor(.15f, .45f, .8f)
                    : FLinearColor::White;
                })
                .OnClicked(this, &SAudioManagerPanel::OnSetPage, Page)
        ];
    }
    return Tabs;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildSourceActions()
{
    TSharedRef<SWrapBox> Bar = SNew(SWrapBox)
        .UseAllottedSize(true);
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("新建选中位置声源")))
            .Visibility_Lambda(
            [this]
            {
                return Controller->GetState().ActivePage == 0 ? EVisibility::Visible
                : EVisibility::Collapsed;
            })
            .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
            .OnClicked(this, &SAudioManagerPanel::OnOpenSourceDialog, EAudioSourceAction::Create)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("修改选中位置声源")))
            .Visibility_Lambda(
            [this]
            {
                return Controller->GetState().ActivePage == 0 ? EVisibility::Visible
                : EVisibility::Collapsed;
            })
            .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
            .OnClicked(this, &SAudioManagerPanel::OnOpenSourceDialog, EAudioSourceAction::Edit)
    ];
    Bar->AddSlot()[BuildSpatialActions()];
    return Bar;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildSpatialActions()
{
    TSharedRef<SWrapBox> Bar = SNew(SWrapBox).UseAllottedSize(true);
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Visibility_Lambda(
            [this]
            {
                return Controller->GetState().ActivePage == 0
                && !Controller->GetState().bProjectAssets
                && !Controller->GetSelectedRooms()
                .IsEmpty()
                ? EVisibility::Visible
                : EVisibility::Collapsed;
            })
            .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
            .Text_Lambda(
            [this]
            {
                return FText::FromString(Controller->GetState().bShowRoomActions
                ? TEXT("收起 Room 底噪设置")
                : TEXT("Room 底噪设置"));
            })
            .OnClicked_Lambda(
            [this]
            {
                Controller->ToggleRoomActions();
                return FReply::Handled();
            })
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("编辑形状 / 对象")))
            .Visibility_Lambda(
            [this]
            {
                return Controller->GetState().ActivePage == 0 ? EVisibility::Visible
                : EVisibility::Collapsed;
            })
            .OnClicked(this, &SAudioManagerPanel::OnEditShapes)
    ];
    Bar->AddSlot()
        .Padding(2)
    [
        SNew(SButton)
            .Text(FText::FromString(TEXT("修复 IF 配对")))
            .ToolTipText(FText::FromString(TEXT(
            "仅在自动配对有歧义时使用：Outliner 选择一个 IF 来源和一个已有音频声源。更新几何并保留 Event。正常生成和更新自动同步。")))
            .Visibility_Lambda(
            [this]
            {
                return Controller->GetState().ActivePage == 0 ? EVisibility::Visible
                : EVisibility::Collapsed;
            })
            .IsEnabled(this, &SAudioManagerPanel::OnCanEditRoomSources)
            .OnClicked(this,
            &SAudioManagerPanel::OnUpdateImportedGeometry,
            EGeometryImportTransform::AdoptSource)
    ];
    return Bar;
}

TSharedRef<SWidget> SAudioManagerPanel::BuildToolbar()
{
    TSharedRef<SWrapBox> Bar = SNew(SWrapBox)
        .PreferredSize(1200.f)
        .UseAllottedSize(true);
    Bar->AddSlot()
        .VAlign(VAlign_Top)
        .Padding(2)
    [
        BuildSquareToolbarAction(FText::FromString(TEXT("刷新")),
            FText::FromString(TEXT("刷新列表")),
            FOnClicked::CreateSP(this, &SAudioManagerPanel::OnRefresh))
    ];
    Bar->AddSlot()
        .VAlign(VAlign_Top)
        .Padding(2)
    [
        BuildSquareToolbarAction(FText::FromString(TEXT("打开")),
            FText::FromString(TEXT("打开所选资产或关联 DT；没有资产时定位地图中的 Actor。多选逐项处理。")),
            FOnClicked::CreateSP(this, &SAudioManagerPanel::OnOpenSelection),
            TAttribute<bool>::CreateLambda(
                [this]
                {
                    return Controller->CanEditDetails() && List && List->GetNumItemsSelected() > 0;
                }))
    ];
    Bar->AddSlot()
    [
        BuildSourceActions()
    ];
    return Bar;
}
