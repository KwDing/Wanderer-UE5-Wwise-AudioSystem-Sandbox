#include "Features/AudioManager/View/AudioManagerPanel.h"

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Features/AudioManager/Controller/AudioManagerController.h"
#include "Features/AudioManager/AudioManagerData.h"
#include "Features/AudioManager/Visualization/AudioVisualizationController.h"

TSharedRef<SWidget> SAudioManagerPanel::BuildVisualizationControls()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(4)
    [
        BuildVisualizationToggle()
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(2)
    [
        BuildVisualizationRange()
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildVisualizationRange()
{
    const TWeakPtr<FAudioVisualizationController> ControllerWeakPtr =
        Controller->GetVisualizationController();
    return SNew(SNumericEntryBox<float>)
        .MinValue(AudioVisualizationSettings::RangeMinM)
        .MaxValue(AudioVisualizationSettings::RangeMaxM)
        .AllowSpin(true)
        .IsEnabled_Lambda(
        [ControllerWeakPtr]
        {
            const TSharedPtr<FAudioVisualizationController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->IsEnabled();
        })
        .Value_Lambda(
        [ControllerWeakPtr]() -> TOptional<float>
        {
            const TSharedPtr<FAudioVisualizationController> Controller = ControllerWeakPtr.Pin();
            return Controller ? TOptional<float>(Controller->GetRangeM()) : TOptional<float>();
        })
        .OnValueChanged_Lambda(
        [ControllerWeakPtr](float InValue)
        {
            const TSharedPtr<FAudioVisualizationController> Controller = ControllerWeakPtr.Pin();
            if (Controller)
            {
                Controller->SetRangeM(InValue);
            }
        })
        .ToolTipText(FText::FromString(FString::Printf(
            TEXT("PIE声源可视化范围（米），默认%.0f。按角色到声源形状计算；"
        "只影响绘图，不改音频距离，不受列表选择/筛选限制。"),
            AudioVisualizationSettings::DefaultRangeM)))
        .Label()
    [
        SNew(STextBlock)
            .Text(FText::FromString(TEXT("附近范围（米）")))
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildVisualizationToggle()
{
    const TWeakPtr<FAudioVisualizationController> ControllerWeakPtr =
        Controller->GetVisualizationController();
    const TWeakPtr<SAudioManagerPanel> PanelWeakPtr = SharedThis(this);
    return SNew(SCheckBox)
        .IsChecked_Lambda(
        [ControllerWeakPtr]
        {
            const TSharedPtr<FAudioVisualizationController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->IsEnabled() ? ECheckBoxState::Checked
            : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda(
        [ControllerWeakPtr, PanelWeakPtr](ECheckBoxState InState)
        {
            const TSharedPtr<FAudioVisualizationController> Controller = ControllerWeakPtr.Pin();
            if (Controller)
            {
                Controller->SetEnabled(InState == ECheckBoxState::Checked);
            }
            if (const TSharedPtr<SAudioManagerPanel> Panel = PanelWeakPtr.Pin())
            {
                Panel->Controller->UpdateSummary();
            }
        })
        .ToolTipText(FText::FromString(TEXT("PIE显示附近声源的实际发声点和角色连线，不受列表筛选限制。"
        "最小化后继续绘制；取消勾选或关闭窗口停止。")))
    [
        SNew(STextBlock)
            .Text(FText::FromString(TEXT("附近声源 / 发声连线（PIE）")))
    ];
}
