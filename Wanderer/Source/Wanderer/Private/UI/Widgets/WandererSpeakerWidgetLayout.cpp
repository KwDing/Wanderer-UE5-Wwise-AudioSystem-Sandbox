#include "UI/Widgets/WandererSpeakerWidget.h"

#include "Engine/GameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Data/WandererSpeakerSettings.h"
#include "Systems/Audio/WandererHeadphoneSubsystem.h"
#include "UI/Widgets/RadioItemWidget.h"

namespace
{
    const FLinearColor Glass(0.025f, 0.035f, 0.05f, 0.18f);
    const FLinearColor Blue(0.08f, 0.32f, 0.62f, 0.90f);
    const FLinearColor Red(0.40f, 0.015f, 0.025f, 0.90f);
}

TSharedRef<SWidget> UWandererSpeakerWidget::RebuildWidget()
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RadioBrush.ImageSize = FVector2D(120, 120);
    MountedBrush.ImageSize = RadioBrush.ImageSize;
    RadioBrush.DrawAs = ESlateBrushDrawType::Image;
    MountedBrush.DrawAs = RadioBrush.DrawAs;
    return SNew(SOverlay)
        .Visibility_UObject(this, &UWandererSpeakerWidget::GetHUDVisibility)
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Bottom)
        .Padding(24)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(16, 8))
            .ButtonColorAndOpacity(Glass)
            .OnClicked(FOnClicked::CreateUObject(this, &UWandererSpeakerWidget::OnTogglePanel))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Items (E)")))
            ]
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(0, 0, 0, 20)
        [
            BuildInventory()
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateUObject(
            this, &UWandererSpeakerWidget::GetSelectionPadding)))
        [
            BuildSelectionActions()
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(0, 0, 0, 390)
        [
            BuildPlacementActions()
        ];
}

void UWandererSpeakerWidget::AddInventoryActions(TSharedRef<SVerticalBox> InOptions)
{
    const TAttribute<FText> PlaybackText = TAttribute<FText>::Create(
        TAttribute<FText>::FGetter::CreateUObject(this, &UWandererSpeakerWidget::GetPlaybackText));
    InOptions->AddSlot()
        .AutoHeight()
        .Padding(0, 3)
        [
            BuildActionButton(PlaybackText, &UWandererSpeakerWidget::OnTogglePlayback, Glass)
        ];
    InOptions->AddSlot()
        .AutoHeight()
        .Padding(0, 3)
        [
            SNew(SBox)
            .IsEnabled_UObject(this, &UWandererSpeakerWidget::CanRetrieveSelected)
            [
                BuildActionButton(FText::FromString(TEXT("Retrieve selected")),
                    &UWandererSpeakerWidget::OnRetrieveSelected, Red)
            ]
        ];
    InOptions->AddSlot()
        .AutoHeight()
        .Padding(0, 3)
        [
            BuildActionButton(FText::FromString(TEXT("Retrieve all / Pause")),
                &UWandererSpeakerWidget::OnRetrieveAll, Red)
        ];
    InOptions->AddSlot()
        .AutoHeight()
        .Padding(0, 6)
        [
            SNew(STextBlock)
            .Text_UObject(this, &UWandererSpeakerWidget::GetStatusText)
            .AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildActionButton(
    TAttribute<FText> InText, FRadioButtonCallback InClicked, FLinearColor InColor)
{
    return SNew(SButton)
        .IsFocusable(false)
        .ButtonColorAndOpacity(InColor)
        .OnClicked(FOnClicked::CreateUObject(this, InClicked))
        [
            SNew(STextBlock)
            .Text(InText)
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildInventory()
{
    return SNew(SBox)
        .WidthOverride(820)
        .Visibility_UObject(this, &UWandererSpeakerWidget::GetPanelVisibility)
        [
            SAssignNew(InventoryBorder, SBorder)
            .Padding(12)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(Glass)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    BuildItemCard(EWandererSpeakerItem::Radio)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(8, 0)
                [
                    BuildItemCard(EWandererSpeakerItem::Mounted)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1)
                .Padding(10, 0)
                [
                    BuildInventoryOptions()
                ]
            ]
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildInventoryHeader()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1)
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("ITEMS  /  E to close")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SCheckBox)
            .IsFocusable(false)
            .IsChecked_UObject(this, &UWandererSpeakerWidget::GetHeadphoneCheckState)
            .OnCheckStateChanged_UObject(this, &UWandererSpeakerWidget::OnSetHeadphoneCheckState)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Using Headphone")))
            ]
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildInventoryOptions()
{
    TSharedRef<SVerticalBox> Options = SNew(SVerticalBox);
    Options->AddSlot()
        .AutoHeight()
        .Padding(0, 0, 0, 8)
        [
            BuildInventoryHeader()
        ];
    Options->AddSlot()
        .AutoHeight()
        [
            SAssignNew(DeviceList, SVerticalBox)
        ];
    for (const FWandererSpeakerOption& Option : GetDefault<UWandererSpeakerSettings>()->Options)
    {
        Options->AddSlot()
            .AutoHeight()
            .Padding(0, 2)
            [
                SNew(SButton)
                .IsFocusable(false)
                .ButtonColorAndOpacity(Glass)
                .OnClicked(FOnClicked::CreateUObject(
                    this, &UWandererSpeakerWidget::OnSelectOption, Option.ID))
                [
                    SNew(STextBlock)
                    .Text_UObject(this, &UWandererSpeakerWidget::GetOptionText,
                        Option.ID, Option.Label)
                ]
            ];
    }
    AddInventoryActions(Options);
    return Options;
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildItemCard(EWandererSpeakerItem InItem)
{
    return SNew(SRadioItemWidget)
        .Label_UObject(this, &UWandererSpeakerWidget::GetItemText, InItem)
        .Available_UObject(this, &UWandererSpeakerWidget::CanDeployItem, InItem)
        .Image(InItem == EWandererSpeakerItem::Radio ? &RadioBrush : &MountedBrush)
        .OnDeploy(FOnClicked::CreateUObject(this, &UWandererSpeakerWidget::OnDeployItem, InItem));
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildPlacementActions()
{
    return SAssignNew(PlacementBorder, SBorder)
        .Padding(8)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(Glass)
        .Visibility_UObject(this, &UWandererSpeakerWidget::GetPlacementVisibility)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4)
            [
                BuildActionButton(FText::FromString(TEXT("Place (click / F)")),
                    &UWandererSpeakerWidget::OnConfirmPlacement, Blue)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4)
            [
                BuildActionButton(FText::FromString(TEXT("Rotate (R)")),
                    &UWandererSpeakerWidget::OnRotatePlacement, Glass)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(4)
            [
                BuildActionButton(FText::FromString(TEXT("Cancel (Esc)")),
                    &UWandererSpeakerWidget::OnCancelPlacement, Red)
            ]
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildSelectionActions()
{
    return SAssignNew(ActionBorder, SBorder)
        .Padding(12)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(Glass)
        .Visibility_UObject(this, &UWandererSpeakerWidget::GetSelectionVisibility)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Center)
            .Padding(0, 0, 0, 6)
            [
                SNew(STextBlock)
                .Text_UObject(this, &UWandererSpeakerWidget::GetSelectionText)
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                BuildSelectionButtons()
            ]
        ];
}

TSharedRef<SWidget> UWandererSpeakerWidget::BuildSelectionButtons()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .Padding(0, 0, 8, 0)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(22, 9))
            .ButtonColorAndOpacity(Blue)
            .IsEnabled_UObject(this, &UWandererSpeakerWidget::CanRetrieveSelected)
            .OnClicked(FOnClicked::CreateUObject(this, &UWandererSpeakerWidget::OnRetrieveSelected))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Retrieve")))
            ]
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(22, 9))
            .ButtonColorAndOpacity(Red)
            .OnClicked(FOnClicked::CreateUObject(this, &UWandererSpeakerWidget::OnCancelSelection))
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Cancel")))
            ]
        ];
}

ECheckBoxState UWandererSpeakerWidget::GetHeadphoneCheckState() const
{
    const UWandererHeadphoneSubsystem* Settings =
        UGameInstance::GetSubsystem<UWandererHeadphoneSubsystem>(GetGameInstance());
    return Settings && Settings->IsUsingHeadphone() ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

EVisibility UWandererSpeakerWidget::GetPlacementVisibility() const
{
    return State.bPlacing ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

FMargin UWandererSpeakerWidget::GetSelectionPadding() const
{
    return FMargin(0, 0, 0, State.bOpen ? 390 : 45);
}

void UWandererSpeakerWidget::OnSetHeadphoneCheckState(ECheckBoxState InState)
{
    if (UWandererHeadphoneSubsystem* Settings =
        UGameInstance::GetSubsystem<UWandererHeadphoneSubsystem>(GetGameInstance()))
    {
        Settings->TrySetUsingHeadphone(InState == ECheckBoxState::Checked);
    }
}

void UWandererSpeakerWidget::RefreshDeviceList()
{
    if (!DeviceList)
    {
        return;
    }
    TArray<FRadioDeviceSnapshot> Devices = State.Devices;
    Devices.Sort([](const FRadioDeviceSnapshot& InA, const FRadioDeviceSnapshot& InB)
    {
        return InA.Number < InB.Number;
    });
    TArray<FGuid> IDs;
    for (const FRadioDeviceSnapshot& Device : Devices)
    {
        IDs.Add(Device.ID);
    }
    if (DisplayedSources == IDs && DeviceList->NumSlots() > 0)
    {
        return;
    }
    DisplayedSources = IDs;
    DeviceList->ClearChildren();
    if (IDs.IsEmpty())
    {
        DeviceList->AddSlot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("No deployed devices")))
            ];
    }
    for (const FGuid& ID : IDs)
    {
        DeviceList->AddSlot()
            .AutoHeight()
            .Padding(0, 2)
            [
                SNew(SButton)
                .IsFocusable(false)
                .ButtonColorAndOpacity(Blue)
                .OnClicked(FOnClicked::CreateUObject(
                    this, &UWandererSpeakerWidget::OnSelectDevice, ID))
                [
                    SNew(STextBlock)
                    .Text_UObject(this, &UWandererSpeakerWidget::GetDeviceText, ID)
                ]
            ];
    }
}
