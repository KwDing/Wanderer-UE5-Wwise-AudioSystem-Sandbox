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

class SAudioManagerPanel::SEntryRow : public SMultiColumnTableRow<FEntryPtr>
{
public:
    SLATE_BEGIN_ARGS(SEntryRow)
    {
    }
    SLATE_ARGUMENT(FEntryPtr, Entry)
    SLATE_ARGUMENT(TWeakPtr<SAudioManagerPanel>, ManagerWeakPtr)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArguments, const TSharedRef<STableViewBase>& InOwner)
    {
        Entry = InArguments._Entry;
        ManagerWeakPtr = InArguments._ManagerWeakPtr;
        SMultiColumnTableRow<FEntryPtr>::Construct(FSuperRowType::FArguments()
            .Padding(3), InOwner);
    }
    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
    {
        // Reused rows read the latest snapshot; bindings own values, never Actors.
        return SNew(SBorder)
            .Padding(FMargin(6, 4))
            .BorderImage(FAppStyle::GetBrush("NoBorder"))
            .OnMouseDoubleClick_Lambda(
            [ManagerWeakPtr = ManagerWeakPtr, Item = Entry, Column](
            const FGeometry&, const FPointerEvent& Event)
            {
                if (Column != "Name" || Event.GetEffectingButton() != EKeys::LeftMouseButton)
                {
                    return FReply::Unhandled();
                }
                if (TSharedPtr<SAudioManagerPanel> Pinned = ManagerWeakPtr.Pin();
                Pinned && Item.IsValid() && Item->ActorWeakPtr.IsValid())
                {
                    return Pinned->OnFocusEntries({Item});
                }
                return FReply::Unhandled();
            })
        [
            SNew(STextBlock)
                .Text_Lambda(
                [Item = Entry, Column]
                {
                    const FString Value = Item->Cells.FindRef(Column);
                    if (!Value.IsEmpty())
                    {
                        return FText::FromString(Value);
                    }
                    const bool bEmptyRoomEvent = Column == "Event" && Item->Kind == TEXT("Room 空间");
                    return FText::FromString(bEmptyRoomEvent ? TEXT("无") : TEXT("—"));
                })
        ];
    }

private:
    FEntryPtr Entry;
    TWeakPtr<SAudioManagerPanel> ManagerWeakPtr;
};

void SAudioManagerPanel::Construct(const FArguments& InArguments)
{
    Controller = MakeShared<FAudioManagerController>();
    ConfigureDetails();
    Header = SNew(SHeaderRow);
    ChildSlot
    [
        BuildContents()
    ];
    Controller->Start(StaticCastSharedRef<IAudioManagerInteraction>(SharedThis(this)));
}

void SAudioManagerPanel::RebuildColumns()
{
    Header->ClearColumns();
    if (!Controller->IsColumnVisible(
        Controller->GetState().ActivePage, Controller->GetState().SortColumn))
    {
        Controller->SetSort("Name", EColumnSortMode::Ascending);
    }
    for (const auto& Column :
        FAudioManagerController::GetColumnDefinitions(Controller->GetState().ActivePage))
    {
        if (!Controller->IsColumnVisible(Controller->GetState().ActivePage, Column.Key))
        {
            continue;
        }
        Header->AddColumn(SHeaderRow::Column(Column.Key)
            .DefaultLabel(FText::FromString(Column.Label))
            .ManualWidth(Column.Width)
            .SortMode_Lambda(
            [this, Key = Column.Key]
            {
                return Controller->GetState().SortColumn == Key
                ? Controller->GetState().SortMode
                : EColumnSortMode::None;
            })
            .OnSort_Lambda(
            [this](EColumnSortPriority::Type, const FName& Key, EColumnSortMode::Type Mode)
            {
                Controller->SetSort(Key, Mode);
            }));
    }
}

TSharedRef<SWidget> SAudioManagerPanel::BuildContents()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
    [
        BuildHeading()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4, 0, 4, 4)
    [
        BuildToolbar()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
    [
        BuildFilters()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
    [
        BuildRoomToolbar()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
    [
        BuildBuildingToolbar()
    ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
    [
        SNew(STextBlock)
            .Text_Lambda(
            [this]
            {
                return Controller->GetState().Summary;
            })
            .AutoWrapText(true)
    ]
        + SVerticalBox::Slot()
        .FillHeight(1)
    [
        BuildTable()
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildHeading()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(1)
    [
        BuildPageTabs()
    ]
        + SHorizontalBox::Slot()
        .AutoWidth()
    [
        SNew(SComboButton)
            .HasDownArrow(false)
            .ToolTipText(FText::FromString(TEXT("显示列设置；对象名称固定显示。偏好只保存在本机编辑器，不修改地图。")))
            .OnGetMenuContent(this, &SAudioManagerPanel::BuildColumnSettings)
            .ButtonContent()
        [
            SNew(SImage)
                .Image(FAppStyle::GetBrush("Icons.Settings"))
        ]
    ];
}

TSharedRef<SWidget> SAudioManagerPanel::BuildTable()
{
    TSharedRef<SWidget> Table = SNew(SBox)
        .WidthOverride_Lambda(
        [this]
        {
            constexpr float ScrollbarWidth = 25;
            float Width = ScrollbarWidth;
            for (const SHeaderRow::FColumn& Column : Header->GetColumns())
            {
                Width += Column.Width.Get();
            }
            return Width;
        })
    [
        SAssignNew(List, SListView<FEntryPtr>)
            .ListItemsSource(&Controller->GetState().Entries)
            .HeaderRow(Header)
            .SelectionMode(ESelectionMode::Multi)
            .OnGenerateRow(this, &SAudioManagerPanel::GenerateRow)
            .OnSelectionChanged(this, &SAudioManagerPanel::OnSelectionChanged)
    ];
    return SNew(SSplitter)
        + SSplitter::Slot()
        .Value( .72f)
    [
        SNew(SScrollBox)
            .Orientation(Orient_Horizontal)
            + SScrollBox::Slot()
        [
            Table
        ]
    ]
        + SSplitter::Slot()
        .Value(.28f)
    [
        Details.ToSharedRef()
    ];
}

void SAudioManagerPanel::ConfigureDetails()
{
    FDetailsViewArgs Args;
    Args.bAllowSearch = true;
    Args.bHideSelectionTip = true;
    Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
        .CreateDetailView(Args);
    DetailsChangedHandle = Details->OnFinishedChangingProperties().AddSP(
        this, &SAudioManagerPanel::OnDetailsChanged);
    Details->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateSPLambda(this,
        [this]
        {
            return Controller->CanEditDetails();
        }));
    Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSPLambda(this,
        [this](const FPropertyAndParent& Item)
        {
            if (Controller->GetState().ActivePage == 0)
            {
                return true;
            }
            if (Controller->GetState().ActivePage == 3)
            {
                if (Item.Property.GetMetaData(TEXT("Category")) == TEXT("Building"))
                {
                    return true;
                }
                for (const FProperty* Parent : Item.ParentProperties)
                {
                    if (Parent->GetFName() == TEXT("MemberRooms"))
                    {
                        return true;
                    }
                }
                return false;
            }
            bool bResource = Item.Property.GetFName() == TEXT("ResourceGroups");
            for (const FProperty* Parent : Item.ParentProperties)
            {
                bResource |= Parent->GetFName() == TEXT("ResourceGroups");
            }
            return Controller->GetState().ActivePage == 1
            ? bResource
            : Item.Property.GetMetaData(TEXT("Category")) == TEXT("Audio Budget");
        }));
    Details->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSPLambda(this,
        [this](const FPropertyAndParent& InItem)
        {
            return Controller->IsPropertyReadOnly(InItem);
        }));
}

TSharedRef<ITableRow> SAudioManagerPanel::GenerateRow(
    FEntryPtr InEntry, const TSharedRef<STableViewBase>& InOwner)
{
    return SNew(SEntryRow, InOwner)
        .Entry(InEntry)
        .ManagerWeakPtr(SharedThis(this));
}
