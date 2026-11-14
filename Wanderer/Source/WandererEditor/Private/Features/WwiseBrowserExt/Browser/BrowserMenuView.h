#pragma once

#include "CoreMinimal.h"

#include "Features/WwiseBrowserExt/Browser/BrowserInteraction.h"

class FBrowserSyncController;
class FExtender;
class FMenuBuilder;
struct FWwiseTreeItem;

/** Owns menu registrations and its workflow, including import/drag presentation adapters. */
class FBrowserMenuView : public IBrowserInteraction, public TSharedFromThis<FBrowserMenuView>
{
public:
    virtual ~FBrowserMenuView() override;
    void Start();
    void Stop();
    virtual bool Confirm(const FString& InText) override;
    virtual uint64 GetGeneration() const override;
    virtual bool IsAvailable() const override;
    virtual bool SelectMissingEvents(TArray<FAssetData>& InOutObjects) override;
    virtual void ShowResult(const FString& InText) override;

private:
    TSharedPtr<FBrowserSyncController> Controller;
    uint64 ActivationGeneration = 0;
    FDelegateHandle BrowserMenuHandle;
    FDelegateHandle FolderMenuHandle;

    void OnExtendBrowser(
        FMenuBuilder& InOutMenu, const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);
    TSharedRef<FExtender> OnExtendFolders(const TArray<FString>& InPaths);
    // Menu layout.
    void AddFolderEntry(FBrowserSyncController& InOutController,
        const TArray<FString>& InPaths,
        const TSharedRef<FExtender>& InOutExtender);
    void AddSyncEntries(FBrowserSyncController& InOutController,
        FMenuBuilder& InOutMenu,
        const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);
    void BuildMenu(FBrowserSyncController& InOutController,
        FMenuBuilder& InOutMenu,
        const TArray<TSharedPtr<FWwiseTreeItem>>& InSelection);

};
