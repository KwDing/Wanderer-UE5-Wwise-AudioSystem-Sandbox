#include "Features/WwiseBrowserExt/BrowserPathUtils.h"

FString WwiseBrowserPaths::NormalizePath(FString InPath)
{
    InPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    if (!InPath.StartsWith(TEXT("/")))
    {
        InPath = TEXT("/") + InPath;
    }
    InPath.RemoveFromEnd(TEXT("/"));
    return InPath;
}

bool WwiseBrowserPaths::IsWithin(const FString& InPath, const FString& InRoot)
{
    return !InRoot.IsEmpty() && InRoot != TEXT("/")
           && (InPath.Equals(InRoot, ESearchCase::IgnoreCase)
               || InPath.StartsWith(InRoot + TEXT("/"), ESearchCase::IgnoreCase));
}
