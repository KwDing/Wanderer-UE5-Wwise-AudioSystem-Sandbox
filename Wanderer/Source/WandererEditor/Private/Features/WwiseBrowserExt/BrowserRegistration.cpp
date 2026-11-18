#include "Features/WwiseBrowserExt/Browser/BrowserMenuView.h"
#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    TSharedPtr<FBrowserMenuView> WwiseBrowserMenuView;

    void StartBrowser()
    {
        WwiseBrowserMenuView = MakeShared<FBrowserMenuView>();
        WwiseBrowserMenuView->Start();
    }

    void StopBrowser()
    {
        if (WwiseBrowserMenuView)
        {
            WwiseBrowserMenuView->Stop();
            WwiseBrowserMenuView.Reset();
        }
    }

    FEditorFeatureRegistration WwiseBrowserFeatureRegistration(TEXT("WwiseBrowserExt"), &StartBrowser, &StopBrowser);
} // namespace
