#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Misc/ScopeExit.h"

#include "IAudiokineticTools.h"

#include "Features/WwiseBrowserExt/Browser/BrowserMenuView.h"
#include "Foundation/EditorFeatureRegistration.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrowserRegistrationLifecycleTest,
    "Wanderer.WwiseBrowserSync.RegistrationLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrowserRegistrationLifecycleTest::RunTest(const FString& InParameters)
{
    IAudiokineticTools& Tools =
        FModuleManager::LoadModuleChecked<IAudiokineticTools>(TEXT("AudiokineticTools"));
    ON_SCOPE_EXIT
    {
        FEditorFeatureRegistration::StartAll();
    };

    TestTrue(
        TEXT("Import adapter initially registered"), Tools.OnOverrideWwiseBrowserImport.IsBound());
    FEditorFeatureRegistration::StopAll();
    TestFalse(TEXT("Import unbound after stop"), Tools.OnOverrideWwiseBrowserImport.IsBound());
    TestFalse(TEXT("Drag unbound after stop"), Tools.OnPrepareWwiseBrowserDrag.IsBound());

    FEditorFeatureRegistration::StopAll();
    FEditorFeatureRegistration::StartAll();
    FEditorFeatureRegistration::StartAll();
    TestTrue(TEXT("Import restored after restart"), Tools.OnOverrideWwiseBrowserImport.IsBound());
    TestTrue(TEXT("Drag restored after restart"), Tools.OnPrepareWwiseBrowserDrag.IsBound());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrowserStoppedMenuTest,
    "Wanderer.WwiseBrowserSync.StoppedMenu",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrowserStoppedMenuTest::RunTest(const FString& InParameters)
{
    FEditorFeatureRegistration::StopAll();
    ON_SCOPE_EXIT
    {
        FEditorFeatureRegistration::StartAll();
    };

    const TSharedRef<FBrowserMenuView> View = MakeShared<FBrowserMenuView>();
    View->Start();
    const uint64 Generation = View->GetGeneration();
    FContentBrowserModule& Content =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    const FContentBrowserMenuExtender_SelectedPaths CachedMenu =
        Content.GetAllPathViewContextMenuExtenders().Last();

    View->Stop();
    TestFalse(TEXT("Stopped view unavailable"), View->IsAvailable());
    TestTrue(TEXT("Stopping invalidates modal generation"), View->GetGeneration() != Generation);
    if (TestTrue(TEXT("Cached weak delegate still observes retained view"), CachedMenu.IsBound()))
    {
        const TSharedRef<FExtender> Extender = CachedMenu.Execute({TEXT("/Game/WwiseAudio")});
        TestEqual(
            TEXT("Stopped menu safely returns an empty extender"), Extender->NumExtensions(), 0);
    }
    View->Start();
    TestTrue(TEXT("Same view can restart"), View->IsAvailable());
    View->Stop();
    return true;
}

#endif
