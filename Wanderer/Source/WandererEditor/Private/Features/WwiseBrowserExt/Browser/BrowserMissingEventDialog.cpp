#include "Features/WwiseBrowserExt/Browser/BrowserMissingEventDialog.h"

#include "AssetRegistry/AssetData.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"


namespace
{
    class FBrowserMissingEventSelection
    {
    public:
        bool IsSelected(const FSoftObjectPath& InSoftRef) const;
        void SetSelected(const FSoftObjectPath& InSoftRef, bool bInSelected);
        void Accept();
        bool ApplyTo(TArray<FAssetData>& InOutObjects) const;

    private:
        TSet<FSoftObjectPath> SelectedSoftRefs;
        bool bAccepted = false;
    };

    bool FBrowserMissingEventSelection::IsSelected(const FSoftObjectPath& InSoftRef) const
    {
        return SelectedSoftRefs.Contains(InSoftRef);
    }

    void FBrowserMissingEventSelection::SetSelected(
        const FSoftObjectPath& InSoftRef, bool bInSelected)
    {
        if (bInSelected)
        {
            SelectedSoftRefs.Add(InSoftRef);
        }
        else
        {
            SelectedSoftRefs.Remove(InSoftRef);
        }
    }

    void FBrowserMissingEventSelection::Accept()
    {
        bAccepted = true;
    }

    bool FBrowserMissingEventSelection::ApplyTo(TArray<FAssetData>& InOutObjects) const
    {
        if (!bAccepted)
        {
            return false;
        }
        InOutObjects.RemoveAll(
            [this](const FAssetData& InObject)
            {
                return !IsSelected(InObject.GetSoftObjectPath());
            });
        return !InOutObjects.IsEmpty();
    }

    constexpr int32 DialogWidth = 860;
    constexpr int32 DialogHeight = 500;
    constexpr int32 ContentPadding = 10;
    constexpr int32 ListFillWeight = 1;

    void OnSelectionChanged(ECheckBoxState InState,
        TSharedRef<FBrowserMissingEventSelection> InSelection, FSoftObjectPath InPathSoftRef)
    {
        InSelection->SetSelected(InPathSoftRef, InState == ECheckBoxState::Checked);
    }

    FReply OnAcceptClicked(TSharedRef<FBrowserMissingEventSelection> InSelection,
        TWeakPtr<SWindow> InWindowWeakPtr)
    {
        InSelection->Accept();
        if (const TSharedPtr<SWindow> Pinned = InWindowWeakPtr.Pin())
        {
            Pinned->RequestDestroyWindow();
        }
        return FReply::Handled();
    }

    TSharedRef<SCheckBox> BuildEventRow(
        const FSoftObjectPath& InPathSoftRef,
        const TSharedRef<FBrowserMissingEventSelection>& InSelection)
    {
        return SNew(SCheckBox)
            .IsChecked_Lambda([InSelection, InPathSoftRef]
            {
                return InSelection->IsSelected(InPathSoftRef)
                    ? ECheckBoxState::Checked
                    : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged(FOnCheckStateChanged::CreateStatic(
                &OnSelectionChanged, InSelection, InPathSoftRef))
            [
                SNew(STextBlock)
                .Text(FText::FromString(InPathSoftRef.ToString()))
            ];
    }

    TSharedRef<SScrollBox> BuildEventRows(
        const TArray<FAssetData>& InObjects,
        const TSharedRef<FBrowserMissingEventSelection>& InSelection)
    {
        const TSharedRef<SScrollBox> Rows = SNew(SScrollBox);
        for (const FAssetData& Object : InObjects)
        {
            Rows->AddSlot()
            [
                BuildEventRow(Object.GetSoftObjectPath(), InSelection)
            ];
        }
        return Rows;
    }

    TSharedRef<SButton> BuildAcceptButton(
        const TSharedRef<FBrowserMissingEventSelection>& InSelection,
        const TWeakPtr<SWindow>& InWindowWeakPtr)
    {
        return SNew(SButton)
            .Text(FText::FromString(TEXT("继续删除勾选项")))
            .OnClicked(FOnClicked::CreateStatic(&OnAcceptClicked, InSelection, InWindowWeakPtr));
    }

    TSharedRef<SVerticalBox> BuildDialogContent(
        const TSharedRef<SScrollBox>& InRows, const TSharedRef<SButton>& InAcceptButton)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ContentPadding)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT(
                    "仅删除勾选项，默认全部保留。准备以后补回 Wwise 的 Event 请勿勾选。\n"
                    "继续后将再次检查源对象与 UE 引用，仍被引用的 Event 会保留。")))
            ]
            + SVerticalBox::Slot()
            .FillHeight(ListFillWeight)
            .Padding(ContentPadding)
            [
                InRows
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(ContentPadding)
            [
                InAcceptButton
            ];
    }
}

bool FBrowserMissingEventDialog::SelectMissingEvents(TArray<FAssetData>& InOutObjects)
{
    if (FApp::IsUnattended())
    {
        return false;
    }
    const TSharedRef<FBrowserMissingEventSelection> Selection =
        MakeShared<FBrowserMissingEventSelection>();
    const TSharedRef<SWindow> Window =
        SNew(SWindow)
        .Title(FText::FromString(TEXT("选择要删除的缺失 Event")))
        .ClientSize(FVector2D(DialogWidth, DialogHeight))
        .SupportsMinimize(false)
        .SupportsMaximize(false);
    const TSharedRef<SScrollBox> Rows = BuildEventRows(InOutObjects, Selection);
    const TSharedRef<SButton> AcceptButton = BuildAcceptButton(Selection, Window);
    Window->SetContent(BuildDialogContent(Rows, AcceptButton));
    FSlateApplication::Get().AddModalWindow(Window, FGlobalTabmanager::Get()->GetRootWindow());
    return Selection->ApplyTo(InOutObjects);
}
