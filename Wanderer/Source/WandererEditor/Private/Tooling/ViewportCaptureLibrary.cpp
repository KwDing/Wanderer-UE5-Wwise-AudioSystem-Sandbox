#include "Tooling/ViewportCaptureLibrary.h"

#include "UnrealClient.h"

void UViewportCaptureLibrary::CaptureViewport(const FString& Filename)
{
    if (!IsInGameThread() || Filename.IsEmpty())
    {
        return;
    }
    FScreenshotRequest::RequestScreenshot(Filename, true, false);
}
