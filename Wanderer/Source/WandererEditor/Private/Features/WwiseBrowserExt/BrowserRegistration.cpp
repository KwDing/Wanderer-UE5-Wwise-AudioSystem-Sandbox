#include "Features/WwiseBrowserExt/Browser/BrowserMenuView.h"
#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    TSharedPtr<FBrowserMenuView> View;

    void StartBrowser()
    {
        View = MakeShared<FBrowserMenuView>();
        View->Start();
    }

    void StopBrowser()
    {
        if (View)
        {
            View->Stop();
            View.Reset();
        }
    }

    FEditorFeatureRegistration Registration(TEXT("WwiseBrowserExt"), &StartBrowser, &StopBrowser);
} // namespace
