#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "UObject/CoreRedirects.h"
#include "UObject/UObjectGlobals.h"

#include "Features/GrassRegenerationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrassRegenerationLifecycleTest,
    "Wanderer.EditorTools.GrassRegenerationLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGrassRegenerationLifecycleTest::RunTest(const FString& InParameters)
{
    FGrassRegenerationService Service;
    Service.Start();
    TestTrue(TEXT("Map delegate registered"), Service.MapOpenedHandle.IsValid());
    const FDelegateHandle FirstHandle = Service.MapOpenedHandle;
    Service.Start();
    TestTrue(TEXT("Repeated startup is idempotent"), FirstHandle == Service.MapOpenedHandle);

    Service.OnMapOpened(TEXT("Transient test notification"), false);
    TestTrue(TEXT("Repair scheduled"), Service.GenerationHandle.IsValid());
    TestEqual(TEXT("Bounded repair budget"), Service.RemainingPasses, 2);
    const uint32 FirstGeneration = Service.Generation;
    Service.OnMapOpened(TEXT("Replacement map notification"), false);
    TestTrue(TEXT("New map invalidates old pass"), Service.Generation > FirstGeneration);

    Service.Stop();
    TestFalse(TEXT("Map delegate removed"), Service.MapOpenedHandle.IsValid());
    TestFalse(TEXT("Pending ticker cancelled"), Service.GenerationHandle.IsValid());
    TestEqual(TEXT("Budget cleared"), Service.RemainingPasses, 0);
    TestFalse(TEXT("Late tick rejected"), Service.OnGenerationTick(0.0f));
    Service.OnMapOpened(TEXT("Late notification"), false);
    TestFalse(TEXT("Stopped service cannot reschedule"), Service.GenerationHandle.IsValid());
    Service.Stop();
    Service.Start();
    TestTrue(TEXT("Module restart rebinds"), Service.MapOpenedHandle.IsValid());
    Service.Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEditorToolsRedirectTest,
    "Wanderer.EditorTools.FunctionRedirects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorToolsRedirectTest::RunTest(const FString& InParameters)
{
    TArray<FString> Entries;
    // Core redirect initialization may consume its config section before automation runs.
    if (!FFileHelper::LoadFileToStringArray(
            Entries, *(FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini"))))
    {
        AddError(TEXT("Cannot read the project redirect declarations."));
        return false;
    }
    int32 CheckedCount = 0;
    for (const FString& Entry : Entries)
    {
        FString OldName;
        FString NewName;
        if (!Entry.StartsWith(TEXT("+FunctionRedirects="))
            || !FParse::Value(*Entry, TEXT("OldName="), OldName)
            || !OldName.StartsWith(TEXT("/Script/Wanderer.WandererEditorTools.")))
        {
            continue;
        }
        TestTrue(
            TEXT("Redirect has a destination"), FParse::Value(*Entry, TEXT("NewName="), NewName));
        const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(
            ECoreRedirectFlags::Type_Function, FCoreRedirectObjectName(OldName));
        TestEqual(*OldName, Redirected.ToString(), FCoreRedirectObjectName(NewName).ToString());
        TestNotNull(*NewName, FindObject<UFunction>(nullptr, *NewName));
        ++CheckedCount;
    }
    constexpr int32 ExpectedReflectedEntries = 45;
    TestEqual(TEXT("All migrated reflected entry points covered"),
        CheckedCount,
        ExpectedReflectedEntries);
    return true;
}
#endif
