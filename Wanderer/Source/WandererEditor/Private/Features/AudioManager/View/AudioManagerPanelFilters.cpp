#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "IDetailsView.h"
#include "InputCoreTypes.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
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

TSharedRef<SWidget> SAudioManagerPanel::BuildFilters()
{
    for (const TCHAR* Name : {TEXT("全部类型"),
            TEXT("Room 空间"),
            TEXT("建筑底噪"),
            TEXT("Portal 门洞"),
            TEXT("Room 底声"),
            TEXT("Point 点声源"),
            TEXT("Spline 样条"),
            TEXT("Area 面声源"),
            TEXT("地表预览"),
            TEXT("Global 全局"),
            TEXT("原生 / 设备"),
            TEXT("未配置声源"),
            TEXT("Event 资产"),
            TEXT("DT 资产"),
            TEXT("地图资产")})
    {
        TypeOptions.Add(MakeShared<FString>(Name));
    }

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(4)
    [
        SNew(SCheckBox)
            .IsChecked_Lambda(
            [this]
            {
                return Controller->GetState().bProjectAssets ? ECheckBoxState::Unchecked
                : ECheckBoxState::Checked;
            })
            .OnCheckStateChanged_Lambda(
            [this](ECheckBoxState State)
            {
                Controller->SetProjectAssets(State != ECheckBoxState::Checked);
            })
        [
            SNew(STextBlock)
                .Text(FText::FromString(TEXT("只看当前地图（取消：项目资产）")))
        ]
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
    [
        BuildVisualizationControls()
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        BuildTypeFilter()
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        BuildGroupFilter()
    ]
        + SHorizontalBox::Slot()
        .FillWidth(1)
        .Padding(2)
    [
        SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("按名称、DT 行、Event 搜索；类型请用左侧下拉框")))
            .OnTextChanged_Lambda(
            [this](const FText& Value)
            {
                Controller->SetFilter(Value.ToString());
            })
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildGroupFilter()
{
    return SAssignNew(GroupPicker, SComboBox<TSharedPtr<FString>>)
        .OptionsSource(&Controller->GetState().GroupOptions)
        .IsEnabled_Lambda(
        [this]
        {
            return !Controller->GetState().bProjectAssets;
        })
        .OnGenerateWidget_Lambda(
        [](TSharedPtr<FString> Item)
        {
            return SNew(STextBlock)
            .Text(FText::FromString(*Item));
        })
        .OnSelectionChanged_Lambda(
        [this](TSharedPtr<FString> Item, ESelectInfo::Type)
        {
            if (Item)
            {
                Controller->SetGroupFilter(*Item);
            }
        })
    [
        SNew(STextBlock)
            .Text_Lambda(
            [this]
            {
                return FText::FromString(Controller->GetState().GroupFilter);
            })
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildTypeFilter()
{
    return SNew(SComboBox<TSharedPtr<FString>>)
        .OptionsSource(&TypeOptions)
        .IsEnabled_Lambda(
        [this]
        {
            return Controller->GetState().ActivePage == 0;
        })
        .OnGenerateWidget_Lambda(
        [](TSharedPtr<FString> Item)
        {
            return SNew(STextBlock)
            .Text(FText::FromString(*Item));
        })
        .OnSelectionChanged_Lambda(
        [this](TSharedPtr<FString> Item, ESelectInfo::Type)
        {
            if (Item)
            {
                Controller->SetTypeFilter(*Item);
            }
        })
    [
        SNew(STextBlock)
            .Text_Lambda(
            [this]
            {
                return FText::FromString(Controller->GetState().TypeFilter);
            })
    ];
}
